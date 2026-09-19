#include "taskbar.h"

#include "dumbar.h"
#include "iconresolver.h"
#include "taskbutton.h"
#include "toplevelmanager.h"

#include <QHBoxLayout>
#include <QResizeEvent>

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
}

void TaskBar::addToplevel(Toplevel *toplevel)
{
    if (!toplevel)
        return;

    if (!m_knownToplevels.contains(toplevel)) {
        m_knownToplevels.insert(toplevel);
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
    TaskButton *button = m_buttons.take(toplevel);
    qCDebug(lcDumbar) << "removing toplevel from taskbar"
                             << "output=" << static_cast<void *>(m_output)
                             << "title=" << (toplevel ? toplevel->title() : QStringLiteral("<null>"))
                             << "hadButton=" << (button != nullptr);
    if (!button)
        return;

    m_layout->removeWidget(button);
    button->deleteLater();
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
        m_layout->addWidget(button, 0, Qt::AlignLeft);
        recalculateWidths();
    } else if (!shouldShow && button) {
        m_buttons.remove(toplevel);
        m_layout->removeWidget(button);
        button->deleteLater();
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
