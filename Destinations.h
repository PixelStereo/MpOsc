#pragma once

#include <QHostAddress>
#include <QList>
#include <QString>

struct Destination { QHostAddress ip; quint16 port; };

// Parses "host:port" entries separated by spaces; false when an entry is invalid or none is given.
bool parseDestinations(const QString &text, QList<Destination> &result);
