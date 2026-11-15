#include "Theme.h"

#include <QtWidgets>

void applyInterfacePalette(QApplication &app, Qt::ColorScheme scheme)
{
    const bool dark = scheme == Qt::ColorScheme::Dark;
    const QColor text(dark ? "#f0f4fa" : "#182234");
    const QColor muted(dark ? "#b7c4d6" : "#49586c");
    const QColor accent(dark ? "#435a7d" : "#a9bfdf");
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(dark ? "#252c37" : "#f2f5f9"));
    palette.setColor(QPalette::Base, QColor(dark ? "#1d2430" : "#ffffff"));
    palette.setColor(QPalette::AlternateBase, QColor(dark ? "#303a49" : "#e9edf4"));
    palette.setColor(QPalette::Button, QColor(dark ? "#303947" : "#ffffff"));
    palette.setColor(QPalette::Mid, QColor(dark ? "#7789a3" : "#64758b"));
    palette.setColor(QPalette::Light, QColor(dark ? "#4b586c" : "#ffffff"));
    palette.setColor(QPalette::Midlight, QColor(dark ? "#3a4759" : "#e7ecf4"));
    palette.setColor(QPalette::Dark, QColor(dark ? "#121822" : "#a6b3c6"));
    palette.setColor(QPalette::Shadow, QColor(dark ? "#0b1018" : "#73849b"));
    palette.setColor(QPalette::Highlight, accent);
    palette.setColor(QPalette::Link, QColor(dark ? "#adc8ef" : "#315a89"));
    palette.setColor(QPalette::LinkVisited, QColor(dark ? "#cdb8ef" : "#624587"));
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
