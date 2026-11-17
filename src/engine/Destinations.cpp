#include "Destinations.h"

#include <QRegularExpression>
#include <algorithm>

bool parseDestinations(const QString &text, QList<Destination> &result)
{
    const auto entries = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    for (const QString &entry : entries) {
        const int colon = entry.lastIndexOf(':');
        auto host = entry.left(colon);
        if (host.startsWith('[') && host.endsWith(']')) host = host.mid(1, host.size() - 2);
        bool ok = false;
        const int port = entry.mid(colon + 1).toInt(&ok);
        const QHostAddress ip(host);
        if (colon < 1 || !ok || port < 1 || port > 65535 || ip.isNull()) return false;
        const auto same = [&](const Destination &d) { return d.ip == ip && d.port == port; };
        if (std::none_of(result.begin(), result.end(), same)) result.append({ip, quint16(port)});
    }
    return !result.isEmpty();
}
