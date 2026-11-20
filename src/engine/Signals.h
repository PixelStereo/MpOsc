#pragma once

#include <QJsonObject>
#include <QMap>
#include <QString>

// OSC signals (address -> value) derived from one MediaPipe result.
QMap<QString, double> signalsFor(const QJsonObject &frame);

// What a signal is, as MediaPipe gives it: a switch (a hand or a body is there; a gesture is the one recognized: a category, so on or off),
// a count (the hands seen), or a measure (0 to 1, degrees…). It is its type in the OSC namespace and on the wire: "T"/"F", "i", "f"
enum class SignalType { Float, Int, Bool };
SignalType signalType(const QString &address);
QString signalTypeTag(SignalType type); // the OSC type tag of the namespace ("f", "i", "T")
// The family a signal is listed under: /face, /hands/left, /hands/right, /body (and /hands for the count of hands)
QString signalFamily(const QString &address);
