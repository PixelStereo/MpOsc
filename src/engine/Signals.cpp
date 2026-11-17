#include "Signals.h"

#include <QJsonArray>
#include <QtMath>
#include <cmath>

namespace {
void addHeadRotation(const QJsonArray &matrix, QMap<QString, double> &values)
{
    if (matrix.size() != 16) return;
    double r[3][3];
    // The worker sends a row-major transform. Normalize its basis vectors to remove scale.
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row) {
            const auto value = matrix.at(row * 4 + column);
            if (!value.isDouble() || !std::isfinite(value.toDouble())) return;
            r[row][column] = value.toDouble();
        }
        const auto length = std::hypot(r[0][column], r[1][column], r[2][column]);
        if (length <= 1e-9) return;
        for (int row = 0; row < 3; ++row) r[row][column] /= length;
    }

    // Decompose R = Ry(yaw) * Rx(pitch) * Rz(roll). At the singularity use zero roll.
    const double cosPitch = std::hypot(r[0][2], r[2][2]);
    const double pitch = std::atan2(-r[1][2], cosPitch);
    const bool singular = cosPitch < 1e-6;
    const double yaw = singular ? std::atan2(-r[2][0], r[0][0]) : std::atan2(r[0][2], r[2][2]);
    const double roll = singular ? 0.0 : std::atan2(r[1][0], r[1][1]);
    values["/face/head/yaw"] = qRadiansToDegrees(yaw);
    values["/face/head/pitch"] = qRadiansToDegrees(pitch);
    values["/face/head/roll"] = qRadiansToDegrees(roll);
}
}

QMap<QString, double> signalsFor(const QJsonObject &frame)
{
    QMap<QString, double> values;
    const auto face = frame.value("face").toArray();
    if (frame.contains("face")) values["/face/present"] = !face.isEmpty();
    if (!face.isEmpty()) {
        const auto detection = face.first().toObject();
        addHeadRotation(detection.value("matrix").toArray(), values);
        const auto expressions = detection.value("expressions").toObject();
        for (auto i = expressions.begin(); i != expressions.end(); ++i)
            values["/face/" + i.key()] = i.value().toDouble();
        const auto points = detection.value("points").toArray();
        if (points.size() > 1) {
            const auto nose = points.at(1).toArray();
            values["/face/position/x"] = nose.at(0).toDouble();
            values["/face/position/y"] = nose.at(1).toDouble();
        }
    }
    const auto hands = frame.value("hands").toArray();
    if (frame.contains("hands")) {
        values["/hands/count"] = hands.size();
        for (const auto *side : {"left", "right"}) values[QString("/hands/%1/present").arg(side)] = 0;
    }
    for (const auto &entry : hands) {
        const auto hand = entry.toObject();
        const auto prefix = hand.value("right").toBool() ? QString("/hands/right/") : QString("/hands/left/");
        values[prefix + "present"] = 1;
        values[prefix + "confidence"] = hand.value("score").toDouble();
        const auto points = hand.value("points").toArray();
        if (points.size() >= 9) {
            const auto wrist = points.at(0).toArray(), thumb = points.at(4).toArray(), finger = points.at(8).toArray();
            values[prefix + "wrist/x"] = wrist.at(0).toDouble();
            values[prefix + "wrist/y"] = wrist.at(1).toDouble();
            values[prefix + "pinch"] = std::clamp(1 - 6 * std::hypot(thumb.at(0).toDouble() - finger.at(0).toDouble(),
                                                                    thumb.at(1).toDouble() - finger.at(1).toDouble()), 0.0, 1.0);
        }
        for (const auto *gesture : {"Closed_Fist", "Open_Palm", "Pointing_Up", "Thumb_Up", "Thumb_Down", "Victory", "ILoveYou"})
            values[prefix + "gesture/" + QString::fromLatin1(gesture).toLower()] = hand.value("gesture").toString() == QLatin1String(gesture);
    }
    const auto poses = frame.value("pose").toArray();
    if (frame.contains("pose")) values["/body/present"] = !poses.isEmpty();
    if (!poses.isEmpty()) {
        const auto points = poses.first().toObject().value("points").toArray();
        for (int i = 0; i < points.size(); ++i) {
            const auto point = points.at(i).toArray();
            for (int axis = 0; axis < point.size() && axis < 3; ++axis)
                values[QString("/body/landmark/%1/%2").arg(i).arg("xyz"[axis])] = point.at(axis).toDouble();
        }
    }
    return values;
}

double invertedSignal(const QString &address, double value)
{
    if (address.startsWith("/face/head/") || address.endsWith("/z")) return -value;
    if (address == "/hands/count") return 2.0 - value;
    return 1.0 - value;
}
