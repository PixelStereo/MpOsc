#include "Theme.h"

#include <QtWidgets>
#include <pxs/Theme.h>

void applyInterfacePalette(QApplication &app)
{
    // The palette and the style sheet of the checkable buttons, sections and captions come from pxs; this is MpOsc's own
    pxs::apply(app, pxs::graphite(), QStringLiteral(R"(
QToolButton#signalPin { border:1px solid transparent; border-radius:4px; }
QToolButton#signalPin:hover, QToolButton#signalPin:checked { background:@alternate; }
QToolButton#signalPin:focus { border-color:@focus; }
QProgressBar#signalValue { background:@base; color:@text; border:1px solid @border; border-radius:4px; text-align:center; }
QProgressBar#signalValue::chunk { background:@fill; border-radius:3px; }
QProgressBar#signalValue:disabled { color:@muted; }
QProgressBar#signalValue::chunk:disabled { background:@alternate; }
)"));
}

QIcon signalPinIcon(bool pinned, const QColor &color)
{
    QPixmap icon(32, 32);
    icon.setDevicePixelRatio(2);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(8, 8);
    painter.rotate(pinned ? 0 : 38);
    painter.setPen(QPen(color, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(pinned ? QBrush(color) : QBrush(Qt::NoBrush));
    QPainterPath head;
    head.moveTo(-3, -5);
    head.lineTo(3, -5);
    head.lineTo(2, -1);
    head.lineTo(4, 1);
    head.lineTo(-4, 1);
    head.lineTo(-2, -1);
    head.closeSubpath();
    painter.drawPath(head);
    painter.drawLine(QPointF(0, 1), QPointF(0, 6));
    painter.end();
    return QIcon(icon);
}
