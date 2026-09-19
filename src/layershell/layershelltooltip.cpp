#include "layershelltooltip.h"

#include "../dumbar.h"
#include "popup.h"

#include <QLabel>

namespace
{
constexpr int kTooltipDelay = 500;
}

LayerShellTooltip::LayerShellTooltip(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &LayerShellTooltip::showWindow);
}

void LayerShellTooltip::show(QWidget *anchor, const QString &text)
{
    qCDebug(lcDumbar) << "tooltip request"
                      << "anchor=" << static_cast<void *>(anchor)
                      << "text=" << text;
    m_timer.stop();
    if (!anchor || text.isEmpty() || !anchor->isVisible()) {
        if (isFor(anchor))
            hide(anchor);
        return;
    }

    if (isFor(anchor) && m_window) {
        update(anchor, text);
        return;
    }

    close();
    m_anchor = anchor;
    m_text = text;
    m_timer.start(kTooltipDelay);
}

void LayerShellTooltip::update(QWidget *anchor, const QString &text)
{
    if (!isFor(anchor))
        return;
    if (text.isEmpty() || !anchor->isVisible()) {
        hide(anchor);
        return;
    }

    m_text = text;
    if (!m_window)
        return;

    m_window->setText(m_text);
    m_window->adjustSize();
    LayerShellPopup::configure(m_window,
                               anchor,
                               "tooltip",
                               Qt::BottomEdge,
                               Qt::BottomEdge);
}

void LayerShellTooltip::hide(QWidget *anchor)
{
    if (!isFor(anchor))
        return;

    close();
}

void LayerShellTooltip::close()
{
    m_timer.stop();
    m_anchor.clear();
    m_text.clear();
    closeWindow();
}

bool LayerShellTooltip::isFor(QWidget *anchor) const
{
    return anchor && m_anchor == anchor;
}

void LayerShellTooltip::showWindow()
{
    if (!m_anchor || m_text.isEmpty() || !m_anchor->isVisible()) {
        close();
        return;
    }

    closeWindow();

    auto *tooltip = new QLabel(nullptr, Qt::ToolTip | Qt::FramelessWindowHint);
    tooltip->setAttribute(Qt::WA_DeleteOnClose);
    tooltip->setAttribute(Qt::WA_TransparentForMouseEvents);
    tooltip->setObjectName(QStringLiteral("dumbarTooltip"));
    tooltip->setTextFormat(Qt::PlainText);
    tooltip->setWordWrap(true);
    tooltip->setMaximumWidth(512);
    tooltip->setMargin(6);
    tooltip->setText(m_text);
    tooltip->setStyleSheet(QStringLiteral(
        "QLabel#dumbarTooltip { background: #303030; color: #eeeeee; "
        "border: 1px solid #606060; border-radius: 3px; }"));
    tooltip->adjustSize();

    if (!LayerShellPopup::configure(tooltip,
                                    m_anchor,
                                    "tooltip",
                                    Qt::BottomEdge,
                                    Qt::BottomEdge)) {
        tooltip->deleteLater();
        return;
    }

    m_window = tooltip;
    connect(tooltip, &QObject::destroyed, this, [this, tooltip] {
        if (m_window.data() == tooltip)
            m_window = nullptr;
    });
    tooltip->show();
    qCDebug(lcDumbar) << "showing tooltip"
                      << "text=" << m_text
                      << "anchor=" << static_cast<void *>(m_anchor.data());
}

void LayerShellTooltip::closeWindow()
{
    if (!m_window)
        return;

    m_window->close();
    m_window = nullptr;
}
