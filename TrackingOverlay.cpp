#include "TrackingOverlay.h"

#include <QJsonArray>
#include <QPainter>
#include <QPainterPath>
#include <array>
#include <cmath>
#include <initializer_list>
#include <optional>

namespace {
const QColor ink("#10141c");
const QColor left("#5dd9ff"), right("#ffc16b"), white("#e4edf6");

class Landmarks {
public:
    Landmarks(const QJsonObject &detection, QRectF area, bool mirror)
        : points(detection.value("points").toArray()), visibility(detection.value("visibility").toArray()),
          area(area), mirror(mirror) {}

    std::optional<QPointF> point(int index) const
    {
        if (index >= points.size() || (!visibility.isEmpty() && visibility.at(index).toDouble() < .45)) return {};
        const auto p = points.at(index).toArray();
        if (p.size() < 2 || !p.at(0).isDouble() || !p.at(1).isDouble()) return {};
        const double x = p.at(0).toDouble(), y = p.at(1).toDouble();
        if (!std::isfinite(x) || !std::isfinite(y)) return {};
        return QPointF(area.x() + (mirror ? 1 - x : x) * area.width(), area.y() + y * area.height());
    }

    void chain(QPainter &painter, std::initializer_list<int> indices, const QColor &color, qreal width) const
    {
        QPainterPath path;
        bool continuous = false;
        for (const int index : indices) {
            const auto p = point(index);
            if (!p) { continuous = false; continue; }
            if (continuous) path.lineTo(*p);
            else path.moveTo(*p);
            continuous = true;
        }
        painter.strokePath(path, QPen(ink, width + 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.strokePath(path, QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    }

    void dots(QPainter &painter, std::initializer_list<int> indices, const QColor &color, qreal radius) const
    {
        painter.setPen(QPen(ink, 1.15));
        painter.setBrush(color);
        for (const auto index : indices)
            if (const auto p = point(index)) painter.drawEllipse(*p, radius, radius);
    }

private:
    QJsonArray points, visibility;
    QRectF area;
    bool mirror;
};

void drawFace(QPainter &p, const Landmarks &face)
{
    face.chain(p, {10,338,297,332,284,251,389,356,454,323,361,288,397,365,379,378,400,377,
                   152,148,176,149,150,136,172,58,132,93,234,127,162,21,54,103,67,109,10}, white, 1.3);
    const QColor eyes("#82eddf"), brows("#ffc986"), lips("#ff849c");
    face.chain(p, {33,160,158,133,153,144,33}, eyes, 1.45);
    face.chain(p, {362,385,387,263,373,380,362}, eyes, 1.45);
    face.chain(p, {70,63,105,66,107}, brows, 1.45);
    face.chain(p, {336,296,334,293,300}, brows, 1.45);
    face.chain(p, {61,40,37,0,267,270,291,321,314,17,84,91,61}, lips, 1.55);
    face.chain(p, {61,13,291,14,61}, lips, 1.0);
    face.chain(p, {168,6,1,2}, white, 1.1);
    face.dots(p, {468,473}, eyes, 2.4);
}

void drawHand(QPainter &p, const Landmarks &hand, const QJsonObject &detection, const QRectF &area)
{
    const QColor thumb("#ff8197"), index("#ffc16b"), middle("#82edaa"), ring("#5dd9ff"), pinkie("#c6a3ff");
    hand.chain(p, {0,5,9,13,17,0}, white, 1.65);
    hand.chain(p, {0,1,2,3,4}, thumb, 1.9);
    hand.chain(p, {5,6,7,8}, index, 1.9);
    hand.chain(p, {9,10,11,12}, middle, 1.9);
    hand.chain(p, {13,14,15,16}, ring, 1.9);
    hand.chain(p, {17,18,19,20}, pinkie, 1.9);
    hand.dots(p, {1,2,3,4}, thumb, 2.6);
    hand.dots(p, {5,6,7,8}, index, 2.6);
    hand.dots(p, {9,10,11,12}, middle, 2.6);
    hand.dots(p, {13,14,15,16}, ring, 2.6);
    hand.dots(p, {17,18,19,20}, pinkie, 2.6);
    hand.dots(p, {0}, white, 3.2);

    const auto wrist = hand.point(0);
    if (!wrist || !area.contains(*wrist)) return;
    const bool isRight = detection.value("right").toBool();
    const auto text = QStringLiteral("%1  %2%").arg(isRight ? "Right" : "Left")
                          .arg(qRound(detection.value("score").toDouble() * 100));
    const qreal width = p.fontMetrics().horizontalAdvance(text) + 13;
    if (width > area.width() || area.height() < 21) return;
    const qreal x = wrist->x() + 8 + width < area.right() ? wrist->x() + 8 : wrist->x() - width - 8;
    const QRectF tag(qBound(area.left(), x, area.right() - width),
                     qBound(area.top(), wrist->y() - 21, area.bottom() - 21), width, 20);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(16, 20, 28, 224));
    p.drawRoundedRect(tag, 5, 5);
    p.setPen(isRight ? right : left);
    p.drawText(tag, Qt::AlignCenter, text);
}

void drawPose(QPainter &p, const Landmarks &pose)
{
    pose.chain(p, {11,12,24,23,11}, white, 2);
    pose.chain(p, {11,13,15}, left, 2.3);
    pose.chain(p, {23,25,27,29,31,27}, left, 2.3);
    pose.chain(p, {12,14,16}, right, 2.3);
    pose.chain(p, {24,26,28,30,32,28}, right, 2.3);
    pose.dots(p, {11,13,15,23,25,27,29,31}, left, 3.1);
    pose.dots(p, {12,14,16,24,26,28,30,32}, right, 3.1);
}
}

void drawTrackingOverlay(QPainter &painter, const QRectF &area, const QJsonObject &result, bool mirror)
{
    painter.save();
    painter.setClipRect(area, Qt::IntersectClip);
    painter.setRenderHint(QPainter::Antialiasing);
    for (const auto &value : result.value("pose").toArray())
        drawPose(painter, Landmarks(value.toObject(), area, mirror));
    for (const auto &value : result.value("face").toArray())
        drawFace(painter, Landmarks(value.toObject(), area, mirror));
    for (const auto &value : result.value("hands").toArray()) {
        const auto hand = value.toObject();
        drawHand(painter, Landmarks(hand, area, mirror), hand, area);
    }
    painter.restore();
}
