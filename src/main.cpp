#include "panel.h"
#include "iconresolver.h"
#include "dumbar.h"
#include "toplevelmanager.h"

#include <QApplication>
#include <QDebug>
#include <QHash>
#include <QIcon>
#include <QScreen>
#include <QtGui/qguiapplication_platform.h>

Q_LOGGING_CATEGORY(lcDumbar, "dumbar", QtWarningMsg)

namespace
{
void configureDumbarLogging()
{
    qSetMessagePattern(QStringLiteral("%{time hh:mm:ss.zzz} [%{file}:%{line}] %{message}"));

    const QByteArray value = qgetenv("DUMBAR_DEBUG").trimmed().toLower();
    if (value == "1" || value == "true" || value == "yes")
        QLoggingCategory::setFilterRules(QStringLiteral("dumbar.debug=true"));
}

void configureIconTheme()
{
    QIcon::setThemeName(QString::fromLatin1(DumbarStyle::kDefaultIconTheme));
    qCDebug(lcDumbar) << "using default icon theme" << QIcon::themeName();
}

void logIconTheme()
{
    qCDebug(lcDumbar) << "Qt icon theme"
                      << "name=" << QIcon::themeName()
                      << "fallback=" << QIcon::fallbackThemeName()
                      << "searchPaths=" << QIcon::themeSearchPaths();

    if (QIcon::themeName().isEmpty())
        qCWarning(lcDumbar) << "Qt icon theme is unset; QIcon::fromTheme lookups may return null";
}
}

int main(int argc, char **argv)
{
    configureDumbarLogging();
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("dumbar"));
    app.setApplicationDisplayName(QStringLiteral("dumbar"));
    configureIconTheme();
    logIconTheme();

    qCDebug(lcDumbar) << "starting dumbar"
                             << "platform=" << app.platformName()
                             << "screens=" << app.screens().size();

    if (!app.platformName().startsWith(QStringLiteral("wayland"))) {
        qCritical() << "dumbar requires a Wayland Qt platform; X11 is not supported";
        return 1;
    }

    auto *nativeWayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!nativeWayland) {
        qCritical("dumbar could not access Qt's Wayland connection");
        return 1;
    }

    qCDebug(lcDumbar) << "using Qt Wayland connection"
                             << "display=" << static_cast<void *>(nativeWayland->display())
                             << "seat=" << static_cast<void *>(nativeWayland->seat());

    ToplevelManager toplevelManager(nativeWayland->display(), nativeWayland->seat(), &app);
    IconResolver icons(&app);
    QHash<QScreen *, Panel *> panels;

    const auto addPanel = [&panels, &toplevelManager, &icons](QScreen *screen) {
        if (!screen || panels.contains(screen))
            return;
        qCDebug(lcDumbar) << "adding panel"
                                 << "screen=" << screen->name()
                                 << "geometry=" << screen->geometry()
                                 << "availableGeometry=" << screen->availableGeometry()
                                 << "devicePixelRatio=" << screen->devicePixelRatio();
        auto *panel = new Panel(&toplevelManager, &icons, screen);
        panels.insert(screen, panel);
        panel->show();
    };
    const auto removePanel = [&panels](QScreen *screen) {
        if (Panel *panel = panels.take(screen)) {
            qCDebug(lcDumbar) << "removing panel"
                                     << "screen=" << (screen ? screen->name() : QStringLiteral("<null>"));
            panel->close();
            panel->deleteLater();
        }
    };

    for (QScreen *screen : app.screens())
        addPanel(screen);
    QObject::connect(&app, &QGuiApplication::screenAdded, &app, addPanel);
    QObject::connect(&app, &QGuiApplication::screenRemoved, &app, removePanel);

    const int result = app.exec();
    qCDebug(lcDumbar) << "event loop exited with code" << result;
    qDeleteAll(panels);
    panels.clear();
    return result;
}
