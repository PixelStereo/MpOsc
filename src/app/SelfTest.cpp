#include "SelfTest.h"

#include "Destinations.h"
#include "MediaPipeRuntime.h"
#include "OscCodec.h"
#include "SignalFilters.h"
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
    if (!worker.waitForStarted()) {
        qCritical().noquote() << "Cannot start the bundled MediaPipe interpreter:" << worker.program() << worker.errorString();
        return 1;
    }
    const QByteArray pixels(128 * 128 * 4, '\0');
    worker.write(QByteArray("128 128 14 40 60 1 0\n") + pixels);
    worker.write(QByteArray("128 128 14 40 60 1 0\n") + pixels);
    worker.write(QByteArray("128 128 14 50 50 2 1\n") + pixels);
    worker.closeWriteChannel();
    if (!worker.waitForFinished(60000)) {
        qCritical().noquote() << "MediaPipe self-test did not finish:" << worker.errorString();
        return 2;
    }
    const auto lines = worker.readAllStandardOutput().trimmed().split('\n');
    if (lines.size() != 4 || !QJsonDocument::fromJson(lines[0]).object().value("ready").toBool()) {
        qCritical().noquote() << "Unexpected MediaPipe startup reply:" << lines.join('\n') << worker.readAllStandardError();
        return 3;
    }
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
    // Inversion is a filter of the chain, whatever the type: 1 − x, −x, 2 − x
    const auto inverted = [](const QString &address, double value) {
        SignalFilter filter;
        filter.setChain(invertChain(address));
        return filter.step(value, .033);
    };
    if (std::abs(inverted("/face/jawOpen", 0.25) - 0.75) > 1e-9 || inverted("/face/head/yaw", -30.0) != 30.0
        || inverted("/hands/count", 0.0) != 2.0 || inverted("/hands/left/gesture/victory", 1) != 0)
        return 12;
    {
        // A switch held for a time (Hold), out as on / off; the chain is saved as text and read back
        auto hold = InteractionStage::made(InteractionFilter::Hold);
        hold.a = 0.2;
        SignalFilter filter;
        filter.setChain(chainFromText(chainToText({hold})));
        if (filter.chain().size() != 1 || filter.chain()[0].kind != InteractionFilter::Hold) return 15;
        filter.step(1, .033);
        const auto still = signalValue(SignalType::Bool, filter.step(0, .033));
        for (int i = 0; i < 8; ++i) filter.step(0, .033);
        const auto gone = signalValue(SignalType::Bool, filter.step(0, .033));
        if (still.typeId() != QMetaType::Bool || !still.toBool() || gone.toBool()) return 15;
        if (signalValue(SignalType::Int, 1.6).toInt() != 2 || signalValue(SignalType::Float, .25).typeId() != QMetaType::Double) return 15;
    }
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
