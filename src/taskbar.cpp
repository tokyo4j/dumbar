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
#include <QResizeEvent>

#include <algorithm>

int taskButtonWidth(int availableWidth, int windowCount)
{
    if (windowCount <= 0)
        return 0;
    return qMin(DumbarStyle::kTaskButtonPreferredWidth, qMax(0, availableWidth / windowCount));
}

TaskBar::TaskBar(ToplevelManager *manager, IconResolver *icons, wl_output *output, QWidget *parent)
    : QWidget(parent)
    , m_icons(icons)
    , m_output(output)
    , m_layout(new QHBoxLayout(this))
{
    qCDebug(lcDumbar) << "creating taskbar"
                       << "output=" << static_cast<void *>(m_output)
                       << "manager=" << static_cast<void *>(manager);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
    m_layout->setAlignment(Qt::AlignLeft);
    // The panel gives the taskbar the space left over by the right-hand
    // widgets.  Do not let the sum of the task buttons' preferred widths
    // become the panel's preferred width once enough windows are open.
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    setMinimumWidth(0);
    setAcceptDrops(true);

    m_dropIndicator = new QWidget(this);
    m_dropIndicator->setObjectName(QStringLiteral("dropIndicator"));
    m_dropIndicator->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_dropIndicator->setFixedSize(DumbarStyle::kDropIndicatorWidth, DumbarStyle::kDropIndicatorHeight);
    m_dropIndicator->setStyleSheet(DumbarStyle::dropIndicatorStyleSheet());
    m_dropIndicator->hide();

    if (!manager) {
        return;
    }

    connect(manager, &ToplevelManager::toplevelAdded, this, &TaskBar::addToplevel);
    connect(manager, &ToplevelManager::toplevelRemoved, this, &TaskBar::removeToplevel);
    connect(manager, &ToplevelManager::availableChanged, this, &TaskBar::managerAvailabilityChanged);

    for (Toplevel *toplevel : manager->toplevels())
        addToplevel(toplevel);
    managerAvailabilityChanged(manager->isAvailable());
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
    if (!toplevel || buttonFor(toplevel))
        return;

    auto *button = new TaskButton(toplevel, m_icons, this);
    button->hide();
    m_buttons.append(button);
    m_layout->addWidget(button, 0, Qt::AlignLeft);
    connect(toplevel, &Toplevel::updated, this, &TaskBar::updateToplevel);
    qCDebug(lcDumbar) << "tracking toplevel"
                             << "output=" << static_cast<void *>(m_output)
                             << "title=" << toplevel->title();
    updateToplevel(toplevel);
    recalculateWidths();
}

void TaskBar::removeToplevel(Toplevel *toplevel)
{
    TaskButton *button = buttonFor(toplevel);
    qCDebug(lcDumbar) << "removing toplevel from taskbar"
                             << "output=" << static_cast<void *>(m_output)
                             << "title=" << (toplevel ? toplevel->title() : QStringLiteral("<null>"))
                             << "hadButton=" << (button != nullptr);
    if (!button)
        return;

    if (m_draggedButton == button)
        clearDropIndicator();
    m_buttons.removeOne(button);
    m_layout->removeWidget(button);
    button->deleteLater();
    recalculateWidths();
}

void TaskBar::updateToplevel(Toplevel *toplevel)
{
    TaskButton *button = buttonFor(toplevel);
    if (!button)
        return;

    const bool shouldShow = toplevel->enteredOutput(m_output);
    const bool wasShown = !button->isHidden();
    qCDebug(lcDumbar) << "taskbar output filter"
                             << "output=" << static_cast<void *>(m_output)
                             << "title=" << toplevel->title()
                             << "enteredOutput=" << shouldShow
                             << "hasButton=" << (button != nullptr);
    button->setVisible(shouldShow);
    button->refresh();
    if (wasShown != shouldShow)
        recalculateWidths();
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
    const QList<TaskButton *> buttons = visibleButtons();
    const int width = taskButtonWidth(contentsRect().width(), buttons.size());
    if (width != m_lastLoggedButtonWidth || buttons.size() != m_lastLoggedButtonCount) {
        qCDebug(lcDumbar) << "task button widths"
                                 << "output=" << static_cast<void *>(m_output)
                                 << "taskbarWidth=" << contentsRect().width()
                                 << "buttonCount=" << buttons.size()
                                 << "buttonWidth=" << width;
        m_lastLoggedButtonWidth = width;
        m_lastLoggedButtonCount = buttons.size();
    }
    for (TaskButton *button : buttons)
        button->setFixedWidth(width);
}

void TaskBar::dragEnterEvent(QDragEnterEvent *event)
{
    acceptDrag(event);
}

void TaskBar::dragLeaveEvent(QDragLeaveEvent *event)
{
    Q_UNUSED(event)
    clearDropIndicator();
}

void TaskBar::dragMoveEvent(QDragMoveEvent *event)
{
    acceptDrag(event);
}

void TaskBar::dropEvent(QDropEvent *event)
{
    if (!acceptDrag(event))
        return;

    reorderButton(m_draggedButton, m_dropIndex);
    event->setDropAction(Qt::MoveAction);
    event->accept();
    clearDropIndicator();
}

TaskButton *TaskBar::buttonFor(Toplevel *toplevel) const
{
    for (TaskButton *button : m_buttons) {
        if (button->toplevel() == toplevel)
            return button;
    }
    return nullptr;
}

QList<TaskButton *> TaskBar::visibleButtons(TaskButton *excluded) const
{
    QList<TaskButton *> buttons;
    for (TaskButton *button : m_buttons) {
        if (!button->isHidden() && button != excluded)
            buttons.append(button);
    }
    return buttons;
}

TaskButton *TaskBar::dragSource(const QDropEvent *event) const
{
    if (!event)
        return nullptr;

    auto *source = qobject_cast<TaskButton *>(event->source());
    if (!source || source->parentWidget() != this || !m_buttons.contains(source))
        return nullptr;

    return source;
}

bool TaskBar::acceptDrag(QDropEvent *event)
{
    TaskButton *source = dragSource(event);
    if (!source) {
        clearDropIndicator();
        event->ignore();
        return false;
    }

    m_draggedButton = source;
    updateDropIndicator(event->position().toPoint(), source);
    event->acceptProposedAction();
    return true;
}

void TaskBar::updateDropIndicator(const QPoint &position, TaskButton *source)
{
    if (!source)
        return;

    const QList<TaskButton *> buttons = visibleButtons(source);

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
        return;
    }

    const int boundary = insertionIndex < buttons.size()
        ? buttons.at(insertionIndex)->geometry().left()
        : buttons.constLast()->geometry().right() + 1;
    const int indicatorX = std::clamp(boundary - DumbarStyle::kDropIndicatorWidth / 2,
                                      0,
                                      qMax(0, width() - DumbarStyle::kDropIndicatorWidth));
    const int indicatorHeight = qMin(DumbarStyle::kDropIndicatorHeight, height());
    const int indicatorY = qMax(0, (height() - indicatorHeight) / 2);
    m_dropIndicator->setFixedHeight(indicatorHeight);
    m_dropIndicator->setGeometry(indicatorX,
                                 indicatorY,
                                 DumbarStyle::kDropIndicatorWidth,
                                 indicatorHeight);
    m_dropIndicator->raise();
    m_dropIndicator->show();
}

void TaskBar::clearDropIndicator()
{
    m_draggedButton.clear();
    m_dropIndex = -1;
    m_dropIndicator->hide();
}

void TaskBar::reorderButton(TaskButton *button, int insertionIndex)
{
    if (!button || insertionIndex < 0 || !m_buttons.contains(button))
        return;

    const QList<TaskButton *> visibleOrder = visibleButtons(button);
    insertionIndex = qBound(0, insertionIndex, visibleOrder.size());
    m_buttons.removeOne(button);

    int orderIndex = m_buttons.size();
    if (insertionIndex < visibleOrder.size())
        orderIndex = m_buttons.indexOf(visibleOrder.at(insertionIndex));
    else if (!visibleOrder.isEmpty())
        orderIndex = m_buttons.indexOf(visibleOrder.constLast()) + 1;
    m_buttons.insert(orderIndex, button);
    m_layout->removeWidget(button);
    m_layout->insertWidget(orderIndex, button, 0, Qt::AlignLeft);
    qCDebug(lcDumbar) << "reordered task button"
                             << "output=" << static_cast<void *>(m_output)
                             << "title=" << button->toplevel()->title()
                             << "insertionIndex=" << insertionIndex;
}
