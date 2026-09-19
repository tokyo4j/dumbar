#pragma once

#include <QToolButton>

class IconResolver;
class Toplevel;

class TaskButton final : public QToolButton
{
    Q_OBJECT

public:
    TaskButton(Toplevel *toplevel, IconResolver *icons, QWidget *parent = nullptr);

    Toplevel *toplevel() const { return m_toplevel; }

public:
    void refresh();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void activateOrMinimize();
    void closeToplevel();
    void startDrag();

    Toplevel *m_toplevel = nullptr;
    IconResolver *m_icons = nullptr;
    QIcon m_icon;
    QPoint m_pressPosition;
    bool m_dragStarted = false;
    bool m_dragging = false;
};
