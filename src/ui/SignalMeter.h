#pragma once

#include <QApplication>
#include <algorithm>
#include <cmath>
#include <QPainter>
#include <QProgressBar>
#include <QStyleOptionProgressBar>

// A signal's bar. The fill is the signal as it comes in; once it has filters, the signal as they leave it is a second level in amber
// along the bottom, with a cap at its end. Where the two differ, the filters are at work; no caption is needed.
class SignalMeter : public QProgressBar {
public:
    // `level` is on the bar's own scale; `shown` false: no filter, only the signal
    void setFiltered(bool shown, int level)
    {
        if (shown == showFiltered && level == filteredLevel) return;
        showFiltered = shown;
        filteredLevel = level;
        update();
    }

    // Where the signal as it comes in stands, as a thin mark over a bar that shows something made of it (the bars of the filters)
    void setReference(bool shown, int level)
    {
        if (shown == showReference && level == referenceLevel) return;
        showReference = shown;
        referenceLevel = level;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QProgressBar::paintEvent(event);
        if (maximum() <= minimum()) return;
        QStyleOptionProgressBar option;
        initStyleOption(&option);
        const QRect groove = style()->subElementRect(QStyle::SE_ProgressBarGroove, &option, this).adjusted(1, 1, -1, -1);
        const auto xOf = [&](int level) {
            const double at = std::clamp(double(level - minimum()) / double(maximum() - minimum()), 0.0, 1.0);
            return groove.left() + int(std::lround(at * (groove.width() - 1)));
        };
        QPainter painter(this);
        painter.setPen(Qt::NoPen);
        if (showReference) { // two short ticks at the edges, so that the text stays readable
            QColor tick = palette().color(QPalette::Text);
            tick.setAlphaF(isEnabled() ? 0.8 : 0.4);
            painter.setBrush(tick);
            const int x = xOf(referenceLevel);
            painter.drawRect(QRect(x, groove.top(), 1, 4));
            painter.drawRect(QRect(x, groove.bottom() - 3, 1, 4));
        }
        if (showFiltered) {
            // The filtered signal: a bar along the bottom, in the accent, with a cap at its end: it reads as a second level under the first
            QColor accent(0xe0, 0xb2, 0x5c);
            if (!isEnabled()) accent.setAlphaF(0.8);
            const int x = xOf(filteredLevel);
            painter.setBrush(accent);
            painter.drawRect(QRect(groove.left(), groove.bottom() - 4, x - groove.left() + 1, 5));
            painter.drawRect(QRect(x - 1, groove.bottom() - 8, 3, 9));
        }
    }

private:
    bool showFiltered = false, showReference = false;
    int filteredLevel = 0, referenceLevel = 0;
};
