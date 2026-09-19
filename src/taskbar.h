#pragma once

#include "toplevelmanager.h"

#include <QHash>
#include <QPointer>
#include <QSet>
#include <QWidget>

class IconResolver;
class TaskButton;
struct wl_output;
class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QResizeEvent;

int taskButtonWidth(int availableWidth, int windowCount);

class TaskBar final : public QWidget
{
    Q_OBJECT

public:
    TaskBar(ToplevelManager *manager,
            IconResolver *icons,
            wl_output *output,
            QWidget *parent = nullptr);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void addToplevel(Toplevel *toplevel);
    void removeToplevel(Toplevel *toplevel);
    void updateToplevel(Toplevel *toplevel);
    void managerAvailabilityChanged(bool available);

private:
    TaskButton *dragSource(const QDropEvent *event) const;
    bool updateDropIndicator(const QPoint &position, TaskButton *source);
    void clearDropIndicator();
    void reorderButton(TaskButton *button, int insertionIndex);
    void syncLayoutOrder();
    void recalculateWidths();

    ToplevelManager *m_manager = nullptr;
    IconResolver *m_icons = nullptr;
    wl_output *m_output = nullptr;
    class QHBoxLayout *m_layout = nullptr;
    QHash<Toplevel *, TaskButton *> m_buttons;
    QList<Toplevel *> m_order;
    QSet<Toplevel *> m_knownToplevels;
    QWidget *m_dropIndicator = nullptr;
    QPointer<TaskButton> m_draggedButton;
    QPoint m_lastDragPosition;
    int m_dropIndex = -1;
    int m_lastLoggedButtonWidth = -1;
    int m_lastLoggedButtonCount = -1;
};
