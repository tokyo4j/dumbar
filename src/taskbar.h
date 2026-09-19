#pragma once

#include "toplevelmanager.h"

#include <QHash>
#include <QSet>
#include <QWidget>

class IconResolver;
class TaskButton;
struct wl_output;

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

private slots:
    void addToplevel(Toplevel *toplevel);
    void removeToplevel(Toplevel *toplevel);
    void updateToplevel(Toplevel *toplevel);
    void managerAvailabilityChanged(bool available);

private:
    void recalculateWidths();

    ToplevelManager *m_manager = nullptr;
    IconResolver *m_icons = nullptr;
    wl_output *m_output = nullptr;
    class QHBoxLayout *m_layout = nullptr;
    QHash<Toplevel *, TaskButton *> m_buttons;
    QSet<Toplevel *> m_knownToplevels;
    int m_lastLoggedButtonWidth = -1;
    int m_lastLoggedButtonCount = -1;
};
