#include "taskbutton.h"

#include "iconresolver.h"
#include "toplevelmanager.h"

#include <QMenu>
#include <QMouseEvent>
#include <QPainter>

namespace
{
constexpr int kIconSize = 16;
constexpr int kHorizontalPadding = 4;
constexpr int kIconTextGap = 4;
}

TaskButton::TaskButton(Toplevel *toplevel, IconResolver *icons, QWidget *parent)
    : QToolButton(parent)
    , m_toplevel(toplevel)
    , m_icons(icons)
{
    setAutoRaise(true);
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    setMinimumWidth(0);
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    connect(this, &QToolButton::clicked, this, &TaskButton::activateOrMinimize);
    refresh();
}

void TaskButton::refresh()
{
    if (!m_toplevel)
        return;

    m_icon = m_icons ? m_icons->iconForAppId(m_toplevel->appId()) : QIcon();
    setProperty("active", m_toplevel->activated() && !m_toplevel->minimized());
    setToolTip(m_toplevel->title());
    update();
}

void TaskButton::activateOrMinimize()
{
    if (!m_toplevel)
        return;

    if (m_toplevel->activated())
        m_toplevel->setMinimized();
    else
        m_toplevel->activate(nullptr);
}

void TaskButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    if (!m_toplevel)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::TextAntialiasing);

    const bool active = property("active").toBool();
    const bool hovered = underMouse();
    if (active)
        painter.fillRect(rect(), QColor(255, 255, 255, 32));
    else if (hovered)
        painter.fillRect(rect(), QColor(255, 255, 255, 18));

    const int iconX = kHorizontalPadding;
    const int iconY = (height() - kIconSize) / 2;
    if (!m_icon.isNull())
        m_icon.paint(&painter, QRect(iconX, iconY, kIconSize, kIconSize));

    const int textX = iconX + kIconSize + kIconTextGap;
    const int textWidth = width() - textX - kHorizontalPadding;
    if (textWidth > 8) {
        const QFontMetrics metrics(font());
        const QString title = metrics.elidedText(m_toplevel->title(), Qt::ElideRight, textWidth);
        painter.setPen(palette().color(QPalette::ButtonText));
        painter.drawText(QRect(textX, 0, textWidth, height()), Qt::AlignVCenter | Qt::AlignLeft, title);
    }
}

void TaskButton::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        QMenu menu(this);
        QAction *closeAction = menu.addAction(tr("Close"));
        connect(closeAction, &QAction::triggered, this, [this] {
            if (m_toplevel)
                m_toplevel->close();
        });
        menu.exec(event->globalPosition().toPoint());
        event->accept();
        return;
    }

    QToolButton::mousePressEvent(event);
}
