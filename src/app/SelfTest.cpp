#include "SelfTest.h"

#include "Destinations.h"
#include "MediaPipeRuntime.h"
#include "OscCodec.h"
#include "Signals.h"
#include "Worker.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QUdpSocket>
#include <cmath>

int runSelfTest()
{
    const auto root = runtimeFolder();
    QProcess worker;
    worker.start(pythonExecutable(root), {"-I", "-u", "-c", QString::fromUtf8(workerScript), root});
    if (!worker.waitForStarted()) return 1;
    const QByteArray pixels(128 * 128 * 4, '\0');
    worker.write(QByteArray("128 128 14 40 60 1 0\n") + pixels);
    worker.write(QByteArray("128 128 14 40 60 1 0\n") + pixels);
    worker.write(QByteArray("128 128 14 50 50 2 1\n") + pixels);
    worker.closeWriteChannel();
    if (!worker.waitForFinished(60000)) return 2;
    const auto lines = worker.readAllStandardOutput().trimmed().split('\n');
    if (lines.size() != 4 || !QJsonDocument::fromJson(lines[0]).object().value("ready").toBool()) return 3;
    const auto frame = QJsonDocument::fromJson(lines[1]).object();
    if (!frame.contains("face") || !frame.contains("hands") || !frame.contains("pose")) {
        qCritical().noquote() << lines[1];
        return 4;
    }
    for (int i = 2; i < 4; ++i) {
        const auto sample = QJsonDocument::fromJson(lines[i]).object();
        if (!sample.value("inference_ms").isDouble() || !sample.contains("face") ||
            !sample.contains("hands") || !sample.contains("pose")) {
            qCritical().noquote() << lines[i];
            return 13;
        }
    }
    QList<Destination> destinations;
    if (!parseDestinations("127.0.0.1:5000 10.0.0.2:9999", destinations) || destinations.size() != 2) return 5;
    destinations.clear();
    QUdpSocket first, second, sender;
    if (!first.bind(QHostAddress::LocalHost, 0) || !second.bind(QHostAddress::LocalHost, 0)) return 6;
    const auto addresses = QString("127.0.0.1:%1 127.0.0.1:%2").arg(first.localPort()).arg(second.localPort());
    if (!parseDestinations(addresses, destinations) || destinations.size() != 2) return 7;
    const auto datagram = osc::encode({"/test", "f", {0.5}});
    for (const auto &destination : destinations)
        if (sender.writeDatagram(datagram, destination.ip, destination.port) != datagram.size()) return 8;
    for (auto *receiver : {&first, &second}) {
        if (!receiver->hasPendingDatagrams() && !receiver->waitForReadyRead(3000)) return 9;
        QByteArray received(receiver->pendingDatagramSize(), Qt::Uninitialized);
        receiver->readDatagram(received.data(), received.size());
        if (received != datagram) return 10;
    }
    // A scaled yaw of 30 degrees with translation verifies matrix layout and units.
    const QJsonArray matrix{std::sqrt(3.0), 0, 1, 12, 0, 2, 0, -3,
                            -1, 0, std::sqrt(3.0), -40, 0, 0, 0, 1};
    const auto head = signalsFor(QJsonObject{{"face", QJsonArray{QJsonObject{{"matrix", matrix}}}}});
    if (std::abs(head.value("/face/head/yaw") - 30.0) > 1e-6
        || !head.contains("/face/head/pitch") || std::abs(head.value("/face/head/pitch")) > 1e-6
        || !head.contains("/face/head/roll") || std::abs(head.value("/face/head/roll")) > 1e-6)
        return 11;
    if (invertedSignal("/face/jawOpen", 0.25) != 0.75
        || invertedSignal("/face/head/yaw", -30.0) != 30.0
        || invertedSignal("/hands/count", 0.0) != 2.0)
        return 12;
    // Switches are switches, a count is an integer, the rest are measures
    if (signalType("/hands/left/gesture/thumb_up") != SignalType::Bool || signalType("/hands/right/present") != SignalType::Bool
        || signalType("/face/present") != SignalType::Bool || signalType("/body/present") != SignalType::Bool
        || signalType("/hands/count") != SignalType::Int || signalType("/hands/left/pinch") != SignalType::Float
        || signalType("/face/jawOpen") != SignalType::Float || signalTypeTag(SignalType::Bool) != "T")
        return 13;
    {
        std::vector<osc::Message> on;
        if (!osc::decode(osc::encode({"/hands/left/gesture/victory", "T", {}}), on) || on.size() != 1 || on[0].types != "T"
            || !on[0].args.value(0).toBool())
            return 14;
    }
    qInfo("Packaged inference, head rotation, inversion and two OSC destinations passed");
    return 0;
}
