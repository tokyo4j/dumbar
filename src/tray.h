#pragma once

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/image.h>
#include <gtkmm/popovermenu.h>

#include <memory>
#include <vector>

class StatusNotifierHost;
class TrayItem;

class TrayButton final : public Gtk::Button
{
public:
    explicit TrayButton(TrayItem &item);
    ~TrayButton() override;

    TrayItem &item() const { return m_item; }

private:
    void refresh();
    void pressed(guint button);
    void showMenu();

    TrayItem &m_item;
    Gtk::Image m_icon;
    Gtk::PopoverMenu m_menu;
};

class Tray final : public Gtk::Box
{
public:
    explicit Tray(StatusNotifierHost &host);

private:
    void addItem(TrayItem &item);
    void removeItem(TrayItem &item);
    // Hides the tray when there is nothing to show, so it takes no spacing.
    void updateVisibility();

    std::vector<std::unique_ptr<TrayButton>> m_buttons;
};
