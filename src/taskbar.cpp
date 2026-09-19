#include "taskbar.h"

#include "dumbar.h"
#include "iconresolver.h"
#include "taskbutton.h"
#include "toplevelmanager.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QMimeData>
#include <QResizeEvent>

#include <algorithm>

namespace
{
constexpr int kDropIndicatorWidth = 2;
constexpr int kDropIndicatorHeight = 24;
constexpr auto kTaskButtonMimeType = "application/x-dumbar-task-button";
}

int taskButtonWidth(int availableWidth, int windowCount)
{
    constexpr int kPreferredTaskButtonWidth = 200;
    if (windowCount <= 0)
        return 0;
    return qMin(kPreferredTaskButtonWidth, qMax(0, availableWidth / windowCount));
}

TaskBar::TaskBar(ToplevelManager *manager, IconResolver *icons, wl_output *output, QWidget *parent)
    : QWidget(parent)
    , m_manager(manager)
    , m_icons(icons)
    , m_output(output)
    , m_layout(new QHBoxLayout(this))
{
    qCDebug(lcDumbar) << "creating taskbar"
                             << "output=" << static_cast<void *>(m_output)
                             << "manager=" << static_cast<void *>(m_manager);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
    m_layout->setAlignment(Qt::AlignLeft);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumWidth(0);
    setAcceptDrops(true);

    m_dropIndicator = new QWidget(this);
    m_dropIndicator->setObjectName(QStringLiteral("dropIndicator"));
    m_dropIndicator->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_dropIndicator->setFixedSize(kDropIndicatorWidth, kDropIndicatorHeight);
    m_dropIndicator->setStyleSheet(QStringLiteral(
        "QWidget#dropIndicator { background: rgba(255, 255, 255, 210); border-radius: 1px; }"));
    m_dropIndicator->hide();

    if (!m_manager) {
        return;
    }

    connect(m_manager, &ToplevelManager::toplevelAdded, this, &TaskBar::addToplevel);
    connect(m_manager, &ToplevelManager::toplevelRemoved, this, &TaskBar::removeToplevel);
    connect(m_manager, &ToplevelManager::availableChanged, this, &TaskBar::managerAvailabilityChanged);

    for (Toplevel *toplevel : m_manager->toplevels())
        addToplevel(toplevel);
    managerAvailabilityChanged(m_manager->isAvailable());
}

void TaskBar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    recalculateWidths();
    if (m_draggedButton && m_dropIndicator->isVisible())
        updateDropIndicator(m_lastDragPosition, m_draggedButton);
}

void TaskBar::addToplevel(Toplevel *toplevel)
{
    if (!toplevel)
        return;

    if (!m_knownToplevels.contains(toplevel)) {
        m_knownToplevels.insert(toplevel);
        m_order.append(toplevel);
        connect(toplevel, &Toplevel::updated, this, &TaskBar::updateToplevel);
        qCDebug(lcDumbar) << "tracking toplevel"
                                 << "output=" << static_cast<void *>(m_output)
                                 << "title=" << toplevel->title();
    }
    updateToplevel(toplevel);
}

void TaskBar::removeToplevel(Toplevel *toplevel)
{
    m_knownToplevels.remove(toplevel);
    m_order.removeOne(toplevel);
    TaskButton *button = m_buttons.take(toplevel);
    qCDebug(lcDumbar) << "removing toplevel from taskbar"
                             << "output=" << static_cast<void *>(m_output)
                             << "title=" << (toplevel ? toplevel->title() : QStringLiteral("<null>"))
                             << "hadButton=" << (button != nullptr);
    if (!button)
        return;

    if (m_draggedButton == button)
        clearDropIndicator();
    m_layout->removeWidget(button);
    button->deleteLater();
    syncLayoutOrder();
    recalculateWidths();
}

void TaskBar::updateToplevel(Toplevel *toplevel)
{
    if (!toplevel || !m_knownToplevels.contains(toplevel))
        return;

    const bool shouldShow = toplevel->enteredOutput(m_output);
    TaskButton *button = m_buttons.value(toplevel);
    qCDebug(lcDumbar) << "taskbar output filter"
                             << "output=" << static_cast<void *>(m_output)
                             << "title=" << toplevel->title()
                             << "enteredOutput=" << shouldShow
                             << "hasButton=" << (button != nullptr);
    if (shouldShow && !button) {
        button = new TaskButton(toplevel, m_icons, this);
        m_buttons.insert(toplevel, button);
        syncLayoutOrder();
        recalculateWidths();
    } else if (!shouldShow && button) {
        m_buttons.remove(toplevel);
        m_layout->removeWidget(button);
        button->deleteLater();
        syncLayoutOrder();
        recalculateWidths();
    } else if (button) {
        button->refresh();
    }
}

void TaskBar::managerAvailabilityChanged(bool available)
{
    qCDebug(lcDumbar) << "toplevel manager availability changed"
                             << "output=" << static_cast<void *>(m_output)
                             << "available=" << available;
    // An empty taskbar keeps the optional right-hand widgets aligned to the
    // right edge even when the compositor has no foreign-toplevel protocol.
    setVisible(true);
    if (available)
        recalculateWidths();
}

void TaskBar::recalculateWidths()
{
    const int width = taskButtonWidth(contentsRect().width(), m_buttons.size());
    if (width != m_lastLoggedButtonWidth || m_buttons.size() != m_lastLoggedButtonCount) {
        qCDebug(lcDumbar) << "task button widths"
                                 << "output=" << static_cast<void *>(m_output)
                                 << "taskbarWidth=" << contentsRect().width()
                                 << "buttonCount=" << m_buttons.size()
                                 << "buttonWidth=" << width;
        m_lastLoggedButtonWidth = width;
        m_lastLoggedButtonCount = m_buttons.size();
    }
    for (TaskButton *button : m_buttons)
        button->setFixedWidth(width);
}

void TaskBar::dragEnterEvent(QDragEnterEvent *event)
{
    TaskButton *source = dragSource(event);
    if (!source) {
        event->ignore();
        return;
    }

    m_draggedButton = source;
    updateDropIndicator(event->position().toPoint(), source);
    event->acceptProposedAction();
}

void TaskBar::dragLeaveEvent(QDragLeaveEvent *event)
{
    Q_UNUSED(event)
    clearDropIndicator();
}

void TaskBar::dragMoveEvent(QDragMoveEvent *event)
{
    TaskButton *source = dragSource(event);
    if (!source) {
        clearDropIndicator();
        event->ignore();
        return;
    }

    m_draggedButton = source;
    updateDropIndicator(event->position().toPoint(), source);
    event->acceptProposedAction();
}

void TaskBar::dropEvent(QDropEvent *event)
{
    TaskButton *source = dragSource(event);
    if (!source) {
        event->ignore();
        return;
    }

    m_draggedButton = source;
    updateDropIndicator(event->position().toPoint(), source);
    reorderButton(source, m_dropIndex);
    event->setDropAction(Qt::MoveAction);
    event->accept();
    clearDropIndicator();
}

TaskButton *TaskBar::dragSource(const QDropEvent *event) const
{
    if (!event || !event->mimeData() || !event->mimeData()->hasFormat(kTaskButtonMimeType))
        return nullptr;

    auto *source = qobject_cast<TaskButton *>(event->source());
    if (!source || source->parentWidget() != this || !m_buttons.contains(source->toplevel())
        || m_buttons.value(source->toplevel()) != source) {
        return nullptr;
    }

    return source;
}

bool TaskBar::updateDropIndicator(const QPoint &position, TaskButton *source)
{
    if (!source)
        return false;

    QList<TaskButton *> buttons;
    for (Toplevel *toplevel : m_order) {
        TaskButton *button = m_buttons.value(toplevel);
        if (button && button != source)
            buttons.append(button);
    }

    int insertionIndex = buttons.size();
    for (int i = 0; i < buttons.size(); ++i) {
        if (position.x() < buttons.at(i)->geometry().center().x()) {
            insertionIndex = i;
            break;
        }
    }
    m_dropIndex = insertionIndex;
    m_lastDragPosition = position;

    if (buttons.isEmpty()) {
        m_dropIndicator->hide();
        return true;
    }

    const int boundary = insertionIndex < buttons.size()
        ? buttons.at(insertionIndex)->geometry().left()
        : buttons.constLast()->geometry().right() + 1;
    const int indicatorX = std::clamp(boundary - kDropIndicatorWidth / 2,
                                      0,
                                      qMax(0, width() - kDropIndicatorWidth));
    const int indicatorHeight = qMin(kDropIndicatorHeight, height());
    const int indicatorY = qMax(0, (height() - indicatorHeight) / 2);
    m_dropIndicator->setFixedHeight(indicatorHeight);
    m_dropIndicator->setGeometry(indicatorX, indicatorY, kDropIndicatorWidth, indicatorHeight);
    m_dropIndicator->raise();
    m_dropIndicator->show();
    return true;
}

void TaskBar::clearDropIndicator()
{
    m_draggedButton.clear();
    m_dropIndex = -1;
    m_dropIndicator->hide();
}

void TaskBar::reorderButton(TaskButton *button, int insertionIndex)
{
    if (!button || insertionIndex < 0)
        return;

    Toplevel *moved = button->toplevel();
    if (!moved || !m_order.contains(moved))
        return;

    QList<Toplevel *> visibleOrder;
    for (Toplevel *toplevel : m_order) {
        if (toplevel != moved && m_buttons.contains(toplevel))
            visibleOrder.append(toplevel);
    }
    insertionIndex = qBound(0, insertionIndex, visibleOrder.size());

    m_order.removeOne(moved);
    int orderIndex = m_order.size();
    if (insertionIndex < visibleOrder.size()) {
        orderIndex = m_order.indexOf(visibleOrder.at(insertionIndex));
    } else if (!visibleOrder.isEmpty()) {
        orderIndex = m_order.indexOf(visibleOrder.constLast()) + 1;
    }
    m_order.insert(orderIndex, moved);
    syncLayoutOrder();
    qCDebug(lcDumbar) << "reordered task button"
                             << "output=" << static_cast<void *>(m_output)
                             << "title=" << moved->title()
                             << "insertionIndex=" << insertionIndex;
}

void TaskBar::syncLayoutOrder()
{
    for (TaskButton *button : m_buttons)
        m_layout->removeWidget(button);

    int layoutIndex = 0;
    for (Toplevel *toplevel : m_order) {
        TaskButton *button = m_buttons.value(toplevel);
        if (button)
            m_layout->insertWidget(layoutIndex++, button, 0, Qt::AlignLeft);
    }
}
