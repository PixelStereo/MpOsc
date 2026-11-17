#pragma once

#include <QJsonObject>
#include <QMap>
#include <QString>

// OSC signals (address -> value) derived from one MediaPipe result.
QMap<QString, double> signalsFor(const QJsonObject &frame);
double invertedSignal(const QString &address, double value);
