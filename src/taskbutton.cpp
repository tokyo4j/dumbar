#include "taskbutton.h"

#include "iconresolver.h"
#include "toplevelmanager.h"

#include <QApplication>
#include <QDrag>
#include <QMimeData>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>

namespace
{
constexpr int kIconSize = 16;
constexpr int kHorizontalPadding = 4;
constexpr int kIconTextGap = 4;
constexpr int kDragChipSize = 26;
constexpr auto kTaskButtonMimeType = "application/x-dumbar-task-button";
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

void TaskButton::closeToplevel()
{
    if (m_toplevel)
        m_toplevel->close();
}

void TaskButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    if (!m_toplevel)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::TextAntialiasing);

    const bool active = m_toplevel->activated() && !m_toplevel->minimized();
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

    if (m_dragging)
        painter.fillRect(rect(), QColor(0, 0, 0, 96));
}

void TaskButton::mousePressEvent(QMouseEvent *event)
{
    switch (event->button()) {
    case Qt::LeftButton:
        m_pressPosition = event->position().toPoint();
        m_dragStarted = false;
        QToolButton::mousePressEvent(event);
        return;
    case Qt::MiddleButton:
        closeToplevel();
        event->accept();
        return;
    case Qt::RightButton: {
        QMenu menu(this);
        menu.addAction(tr("Close"), this, &TaskButton::closeToplevel);
        menu.exec(event->globalPosition().toPoint());
        event->accept();
        return;
    }
    default:
        QToolButton::mousePressEvent(event);
        return;
    }
}

void TaskButton::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint position = event->position().toPoint();
    if (!m_dragStarted && (event->buttons() & Qt::LeftButton)
        && (position - m_pressPosition).manhattanLength() >= QApplication::startDragDistance()) {
        m_dragStarted = true;
        setDown(false);
        startDrag();
        return;
    }

    QToolButton::mouseMoveEvent(event);
}

void TaskButton::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_dragStarted) {
        m_dragStarted = false;
        event->accept();
        return;
    }

    QToolButton::mouseReleaseEvent(event);
}

void TaskButton::startDrag()
{
    auto *mimeData = new QMimeData;
    mimeData->setData(kTaskButtonMimeType, QByteArrayLiteral("move"));

    m_dragging = true;
    update();

    QPixmap pixmap(kDragChipSize, kDragChipSize);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor(32, 32, 32, 235));
    painter.setPen(QPen(QColor(255, 255, 255, 70), 1));
    painter.drawRoundedRect(QRectF(0.5, 0.5, kDragChipSize - 1, kDragChipSize - 1), 6, 6);
    if (!m_icon.isNull())
        m_icon.paint(&painter, QRect((kDragChipSize - kIconSize) / 2,
                                    (kDragChipSize - kIconSize) / 2,
                                    kIconSize,
                                    kIconSize));

    auto *drag = new QDrag(this);
    drag->setMimeData(mimeData);
    drag->setPixmap(pixmap);
    drag->setHotSpot(QPoint(kDragChipSize / 2, kDragChipSize / 2));
    drag->exec(Qt::MoveAction);

    m_dragging = false;
    update();
}
