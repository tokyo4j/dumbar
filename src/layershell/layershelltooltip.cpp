#include "layershelltooltip.h"

#include "../dumbar.h"
#include "popup.h"

#include <QEvent>
#include <QFont>
#include <QLabel>

LayerShellTooltip::LayerShellTooltip(QObject *parent, QWidget *anchor)
    : QObject(parent)
    , m_anchor(anchor)
{
    Q_ASSERT(m_anchor);
    m_anchor->installEventFilter(this);
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &LayerShellTooltip::showWindow);
}

void LayerShellTooltip::setText(const QString &text)
{
    m_text = text;
    if (m_text.isEmpty()) {
        close();
    } else if (m_window) {
        update();
    }
}

void LayerShellTooltip::show()
{
    qCDebug(lcDumbar) << "tooltip request"
                      << "anchor=" << static_cast<void *>(m_anchor.data())
                      << "text=" << m_text;
    m_timer.stop();
    if (!m_anchor || m_text.isEmpty() || !m_anchor->isVisible()) {
        close();
        return;
    }

    if (m_window) {
        update();
        return;
    }

    m_timer.start(Config::kTooltipDelay);
}

void LayerShellTooltip::update()
{
    if (!m_anchor || m_text.isEmpty() || !m_anchor->isVisible()) {
        close();
        return;
    }

    if (!m_window)
        return;

    m_window->setText(m_text);
    m_window->adjustSize();
    LayerShellPopup::configure(m_window,
                               m_anchor,
                               "tooltip",
                               Qt::BottomEdge,
                               Qt::BottomEdge);
}

void LayerShellTooltip::hide()
{
    close();
}

void LayerShellTooltip::close()
{
    m_timer.stop();
    closeWindow();
}

bool LayerShellTooltip::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_anchor.data()) {
        if (event->type() == QEvent::Enter)
            show();
        else if (event->type() == QEvent::Leave)
            hide();
    }
    return QObject::eventFilter(watched, event);
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
    tooltip->setMaximumWidth(Config::kTooltipMaxWidth);
    tooltip->setMargin(Config::kTooltipMargin);
    QFont tooltipFont = tooltip->font();
    tooltipFont.setPixelSize(Config::kFontSize);
    tooltip->setFont(tooltipFont);
    tooltip->setText(m_text);
    tooltip->setStyleSheet(Config::tooltipStyleSheet());
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
