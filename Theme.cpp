#include "Theme.h"

#include <QtWidgets>

void applyInterfacePalette(QApplication &app)
{
    // Graphite: the soft rounded look with light borders, in neutral greys (no blue cast). Always dark.
    const QColor text("#f2f2f3"), muted("#bdbdc2"), accent("#55555c");
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#262628"));
    palette.setColor(QPalette::Base, QColor("#1e1e20"));
    palette.setColor(QPalette::AlternateBase, QColor("#323235"));
    palette.setColor(QPalette::Button, QColor("#313134"));
    palette.setColor(QPalette::Mid, QColor("#7b7b82"));
    palette.setColor(QPalette::Light, QColor("#4c4c52"));
    palette.setColor(QPalette::Midlight, QColor("#3b3b40"));
    palette.setColor(QPalette::Dark, QColor("#141416"));
    palette.setColor(QPalette::Shadow, QColor("#0c0c0d"));
    palette.setColor(QPalette::Highlight, accent);
    palette.setColor(QPalette::Link, QColor("#d4d4d8"));
    palette.setColor(QPalette::LinkVisited, QColor("#b5b5bb"));
    palette.setColor(QPalette::ToolTipBase, palette.color(QPalette::Base));
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::PlaceholderText, muted);
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::HighlightedText}) {
        palette.setColor(role, text);
        palette.setColor(QPalette::Disabled, role, muted);
    }
    palette.setColor(QPalette::Disabled, QPalette::Highlight, palette.color(QPalette::AlternateBase));
    app.setPalette(palette);

    QString style = QStringLiteral(R"(
QGroupBox#section { border:1px solid @border; border-radius:8px; margin-top:9px; }
QGroupBox#section::title { color:@text; subcontrol-origin:margin; left:11px; padding:0 5px; font-weight:600; }
QLabel#caption { color:@muted; }
QPushButton#toggle { background:@button; color:@text; border:1px solid @border; border-radius:6px; padding:5px 9px; }
QPushButton#toggle:hover, QPushButton#toggle:focus { border-color:@focus; }
QPushButton#toggle:checked { background:@fill; color:@text; border-color:@focus; font-weight:600; }
QPushButton#toggle:disabled { color:@muted; }
QPushButton#signalName { background:transparent; color:@text; text-align:left;
    border:1px solid transparent; border-radius:5px; padding:3px 6px; }
QPushButton#signalName:hover, QPushButton#signalName:focus { background:@alternate; }
QFrame#signalCell { border:1px solid transparent; border-radius:5px; }
QFrame#signalCell[oscEnabled="true"] { border-color:@focus; }
QToolButton#signalPin { border:1px solid transparent; border-radius:4px; }
QToolButton#signalPin:hover, QToolButton#signalPin:checked { background:@alternate; }
QToolButton#signalPin:focus { border-color:@focus; }
QProgressBar#signalValue { background:@base; color:@text; border:1px solid @border; border-radius:4px; text-align:center; }
QProgressBar#signalValue::chunk { background:@fill; border-radius:3px; }
QProgressBar#signalValue:disabled { color:@muted; }
QProgressBar#signalValue::chunk:disabled { background:@alternate; }
)");
    style.replace("@text", text.name()).replace("@muted", muted.name()).replace("@fill", accent.name())
         .replace("@button", palette.color(QPalette::Button).name()).replace("@base", palette.color(QPalette::Base).name())
         .replace("@alternate", palette.color(QPalette::AlternateBase).name()).replace("@border", palette.color(QPalette::Mid).name())
         .replace("@focus", palette.color(QPalette::Link).name());
    app.setStyleSheet(style);
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
