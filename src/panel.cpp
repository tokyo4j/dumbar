#include "panel.h"

#include "audio.h"
#include "battery.h"
#include "clock.h"
#include "dumbar.h"
#include "iconresolver.h"
#include "taskbar.h"
#include "toplevelmanager.h"
#include "tray.h"

#include <QApplication>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QScreen>
#include <QtGui/qguiapplication_platform.h>
#include <QtGui/qscreen_platform.h>

#include <LayerShellQt/Window>

#include <wayland-client.h>

Panel::Panel(ToplevelManager *toplevelManager, IconResolver *icons, QScreen *screen, QWidget *parent)
    : QWidget(parent)
    , m_icons(icons)
    , m_toplevelManager(toplevelManager)
    , m_screen(screen)
{
    setObjectName(QStringLiteral("panel"));
    setWindowTitle(QStringLiteral("dumbar"));
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_NativeWindow);
    setFocusPolicy(Qt::NoFocus);
    setFixedHeight(Config::kPanelHeight);

    QFont panelFont = font();
    panelFont.setPixelSize(Config::kFontSize);
    setFont(panelFont);

    qCDebug(lcDumbar) << "creating panel"
                           << "screen=" << (m_screen ? m_screen->name() : QStringLiteral("<null>"))
                           << "geometry=" << (m_screen ? m_screen->geometry() : QRect());

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Config::kPanelSpacing);

    if (m_screen) {
        if (auto *nativeScreen = m_screen->nativeInterface<QNativeInterface::QWaylandScreen>())
            m_output = nativeScreen->output();
    }
    qCDebug(lcDumbar) << "panel output"
                           << "screen=" << (m_screen ? m_screen->name() : QStringLiteral("<null>"))
                           << "wl_output=" << static_cast<void *>(m_output);
    if (!m_output)
        qWarning() << "dumbar: could not get the Wayland output for" << (m_screen ? m_screen->name() : QString());

    m_taskbar = new TaskBar(m_toplevelManager, m_icons, m_output, this);
    m_tray = new Tray(this);
    m_audio = new Audio(this);
    m_battery = new Battery(this);
    m_clock = new Clock(this);

    layout->addWidget(m_taskbar, 1);
    layout->addWidget(m_tray);
    layout->addWidget(m_audio);
    layout->addWidget(m_battery);
    layout->addWidget(m_clock);

    setStyleSheet(Config::panelStyleSheet());

    configureLayerShell();
}

void Panel::configureLayerShell()
{
    // Creating the native window before showing it lets LayerShellQt configure
    // the existing Qt Wayland surface rather than opening another connection.
    winId();
    QWindow *window = windowHandle();
    if (!window) {
        qWarning("dumbar: could not create the panel's Wayland window");
        return;
    }

    qCDebug(lcDumbar) << "configuring layer-shell panel"
                           << "screen=" << (m_screen ? m_screen->name() : QStringLiteral("<null>"))
                           << "window=" << static_cast<void *>(window);

    LayerShellQt::Window *layerWindow = LayerShellQt::Window::get(window);
    if (!layerWindow) {
        qWarning("dumbar: LayerShellQt could not create a layer-shell surface");
        return;
    }

    layerWindow->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop)
                             | LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight);
    layerWindow->setLayer(LayerShellQt::Window::LayerTop);
    layerWindow->setExclusiveZone(Config::kPanelHeight);
    layerWindow->setDesiredSize(QSize(0, Config::kPanelHeight));
    layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    layerWindow->setScope(QStringLiteral("dumbar"));
    if (m_screen)
        layerWindow->setScreen(m_screen);

    qCDebug(lcDumbar) << "layer-shell panel configured"
                           << "screen=" << (m_screen ? m_screen->name() : QStringLiteral("<null>"))
                           << "anchors=top,left,right"
                           << "height=" << Config::kPanelHeight
                           << "exclusiveZone=" << Config::kPanelHeight;
}
