#pragma once

#include <QToolButton>

class IconResolver;
class Toplevel;

class TaskButton final : public QToolButton
{
    Q_OBJECT

public:
    TaskButton(Toplevel *toplevel, IconResolver *icons, QWidget *parent = nullptr);

public slots:
    void refresh();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private slots:
    void activateOrMinimize();

private:
    Toplevel *m_toplevel = nullptr;
    IconResolver *m_icons = nullptr;
    QIcon m_icon;
};
