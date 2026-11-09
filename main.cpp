#include "OscCodec.h"
#include "Worker.h"

#include <QtWidgets>
#include <QCamera>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QVideoFrame>
#include <QVideoSink>
#include <QUdpSocket>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QPermissions>
#include <memory>
#include <utility>
#include <algorithm>
#include <cmath>

namespace {
struct Destination { QHostAddress ip; quint16 port; };

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

QString runtimeFolder()
{
    const QDir app(QCoreApplication::applicationDirPath());
#ifdef Q_OS_MACOS
    return app.absoluteFilePath("../Resources/mediapipe");
#else
    return app.absoluteFilePath("mediapipe");
#endif
}

QString pythonExecutable(const QString &root)
{
#ifdef Q_OS_WIN
    return QDir(root).filePath("python/python.exe");
#else
    return QDir(root).filePath("python/bin/python3.11");
#endif
}

QMap<QString, double> signalsFor(const QJsonObject &frame)
{
    QMap<QString, double> values;
    const auto face = frame.value("face").toArray();
    if (frame.contains("face")) values["/face/present"] = !face.isEmpty();
    if (!face.isEmpty()) {
        const auto detection = face.first().toObject();
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

class Window : public QWidget {
public:
    Window()
    {
        setWindowTitle("MP_OSC");
        resize(1120, 690);
        auto *layout = new QVBoxLayout(this);
        auto *source = new QHBoxLayout;
        cameraChoice = new QComboBox;
        for (const auto &device : QMediaDevices::videoInputs()) cameraChoice->addItem(device.description(), device.id());
        auto *startCamera = new QPushButton("Camera");
        startCamera->setEnabled(cameraChoice->count() != 0);
        auto *open = new QPushButton("Open video…");
        auto *stop = new QPushButton("Stop");
        mode = new QComboBox;
        mode->addItem("Face", 4);
        mode->addItem("Hands", 2);
        mode->addItem("Body", 8);
        mode->addItem("Face + hands", 6);
        for (auto *w : QList<QWidget *>{cameraChoice, startCamera, open, stop, mode}) source->addWidget(w);
        layout->addLayout(source);

        auto *oscBar = new QHBoxLayout;
        destinations = new QLineEdit(QSettings().value("destinations", "127.0.0.1:5000").toString());
        destinations->setPlaceholderText("127.0.0.1:5000 10.0.0.2:9999");
        send = new QCheckBox("Send OSC");
        oscBar->addWidget(new QLabel("Destinations"));
        oscBar->addWidget(destinations, 1);
        oscBar->addWidget(send);
        layout->addLayout(oscBar);
        auto *split = new QSplitter;
        picture = new QLabel("Choose a camera or a video");
        picture->setAlignment(Qt::AlignCenter);
        picture->setMinimumWidth(250);
        picture->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        split->addWidget(picture);
        auto *right = new QWidget;
        auto *controls = new QVBoxLayout(right);
        auto *search = new QLineEdit;
        search->setPlaceholderText("Filter signals by name");
        controls->addWidget(search);
        table = new QTreeWidget;
        table->setRootIsDecorated(false);
        table->setHeaderLabels({"Signal / OSC address", "Raw", "Output", "Send", "Smooth (ms)", "Deadband", "Threshold"});
        table->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        table->setColumnWidth(1, 65);
        table->setColumnWidth(2, 100);
        controls->addWidget(table);
        split->addWidget(right);
        split->setSizes({520, 600});
        layout->addWidget(split, 1);
        status = new QLabel("Ready");
        layout->addWidget(status);
        capture.setVideoSink(&cameraSink);
        player.setVideoSink(&fileSink);
        player.setLoops(QMediaPlayer::Infinite);

        auto receiveFrame = [this](const QVideoFrame &frame) {
            if (!running || !frame.isValid()) return;
            latest = frame.toImage();
            if (!ready) showFrame(latest);
        };
        connect(&cameraSink, &QVideoSink::videoFrameChanged, this, receiveFrame);
        connect(&fileSink, &QVideoSink::videoFrameChanged, this, receiveFrame);
        auto openCamera = [this] {
            const auto devices = QMediaDevices::videoInputs();
            const auto id = cameraChoice->currentData().toByteArray();
            for (const auto &device : devices)
                if (device.id() == id) {
                    stopSource();
                    capture.setCamera(nullptr);
                    camera.reset(new QCamera(device));
                    connect(camera.get(), &QCamera::errorOccurred, this, [this](QCamera::Error, const QString &error) {
                        status->setText(error);
                    });
                    capture.setCamera(camera.get());
                    running = true;
                    camera->start();
                    return;
                }
        };
        connect(startCamera, &QPushButton::clicked, this, [this, openCamera] {
            const QCameraPermission permission;
            const auto access = qApp->checkPermission(permission);
            if (access == Qt::PermissionStatus::Undetermined) {
                qApp->requestPermission(permission, this, [this, openCamera](const QPermission &result) {
                    if (result.status() == Qt::PermissionStatus::Granted) openCamera();
                    else status->setText("Camera permission denied");
                });
            } else if (access == Qt::PermissionStatus::Granted) {
                openCamera();
            } else {
                status->setText("Camera permission denied");
            }
        });
        connect(open, &QPushButton::clicked, this, [this] {
            const auto path = QFileDialog::getOpenFileName(this, "Open video");
            if (path.isEmpty()) return;
            stopSource();
            running = true;
            player.setSource(QUrl::fromLocalFile(path));
            player.play();
        });
        connect(stop, &QPushButton::clicked, this, [this] { stopSource(); picture->setText("Source stopped"); });
        connect(&player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &error) { status->setText(error); });
        connect(search, &QLineEdit::textChanged, this, [this](const QString &text) {
            query = text;
            for (auto i = rows.begin(); i != rows.end(); ++i) i->item->setHidden(!i.key().contains(query, Qt::CaseInsensitive));
        });
        connect(destinations, &QLineEdit::editingFinished, this, [this] {
            QSettings().setValue("destinations", destinations->text());
            for (auto &row : rows) row.sent = false;
        });
        connect(table, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
            if (column != 3) return;
            auto row = rows.find(item->text(0));
            if (row == rows.end()) return;
            row->sent = false;
            QSettings().setValue(QStringLiteral("signals") + row.key() + "/send", item->checkState(3) == Qt::Checked);
        });
        connect(&worker, &QProcess::readyReadStandardOutput, this, [this] {
            while (worker.canReadLine()) {
                const auto result = QJsonDocument::fromJson(worker.readLine()).object();
                if (result.value("ready").toBool()) { ready = true; status->setText("MediaPipe ready"); continue; }
                busy = false;
                if (!active) continue;
                const QImage annotated = QImage::fromData(QByteArray::fromBase64(result.value("preview_jpeg").toString().toLatin1()));
                showFrame(annotated.isNull() ? submitted : annotated);
                if (submittedMask == mode->currentData().toInt()) updateSignals(result);
            }
        });
        connect(&worker, &QProcess::errorOccurred, this, [this] { status->setText(worker.errorString()); });
        connect(&worker, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this] {
            ready = busy = false;
            status->setText("MediaPipe worker stopped");
        });
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("MPLBACKEND", "Agg");
        worker.setProcessEnvironment(environment);
        worker.setStandardErrorFile(QProcess::nullDevice());
        const auto root = runtimeFolder();
        worker.start(pythonExecutable(root), {"-I", "-u", "-c", QString::fromUtf8(workerScript), root});
        clock.start();
        connect(&timer, &QTimer::timeout, this, [this] {
            if (!ready || busy || latest.isNull()) return;
            submitted = std::exchange(latest, QImage()).scaled(960, 720, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                        .convertToFormat(QImage::Format_RGBA8888);
            submittedMask = mode->currentData().toInt();
            QByteArray packet = QByteArray::number(submitted.width()) + ' ' + QByteArray::number(submitted.height()) +
                                ' ' + QByteArray::number(submittedMask | 4096) + '\n';
            for (int y = 0; y < submitted.height(); ++y)
                packet.append(reinterpret_cast<const char *>(submitted.constScanLine(y)), submitted.width() * 4);
            active = busy = true;
            worker.write(packet);
        });
        timer.start(33);
    }

    ~Window() override
    {
        stopSource();
        worker.closeWriteChannel();
        if (worker.state() != QProcess::NotRunning && !worker.waitForFinished(300)) {
            worker.kill();
            worker.waitForFinished(1000);
        }
    }

private:
    struct Row {
        QTreeWidgetItem *item;
        QProgressBar *meter;
        QSpinBox *smooth;
        QDoubleSpinBox *deadband, *threshold;
        double filtered = 0, lastSent = 0;
        bool initialized = false, sent = false;
    };

    void stopSource()
    {
        running = false;
        if (camera) camera->stop();
        player.stop();
        latest = {};
        active = false;
        for (auto &row : rows) row.initialized = row.sent = false;
    }

    void showFrame(const QImage &image)
    {
        if (!image.isNull()) picture->setPixmap(QPixmap::fromImage(image).scaled(picture->size(), Qt::KeepAspectRatio,
                                                                                Qt::SmoothTransformation));
    }

    Row makeRow(const QString &address)
    {
        auto *item = new QTreeWidgetItem(table, {address});
        item->setCheckState(3, Qt::Unchecked);
        item->setHidden(!address.contains(query, Qt::CaseInsensitive));
        const auto settingsKey = QStringLiteral("signals") + address;
        QSettings settings;
        item->setCheckState(3, settings.value(settingsKey + "/send", false).toBool() ? Qt::Checked : Qt::Unchecked);
        auto *meter = new QProgressBar;
        meter->setRange(0, 1000);
        table->setItemWidget(item, 2, meter);
        auto *smooth = new QSpinBox;
        smooth->setRange(0, 2000);
        smooth->setValue(settings.value(settingsKey + "/smooth", 0).toInt());
        connect(smooth, &QSpinBox::valueChanged, this, [settingsKey](int value) {
            QSettings().setValue(settingsKey + "/smooth", value);
        });
        table->setItemWidget(item, 4, smooth);
        auto *deadband = new QDoubleSpinBox;
        deadband->setRange(0, 1);
        deadband->setDecimals(3);
        deadband->setValue(settings.value(settingsKey + "/deadband", 0).toDouble());
        connect(deadband, &QDoubleSpinBox::valueChanged, this, [settingsKey](double value) {
            QSettings().setValue(settingsKey + "/deadband", value);
        });
        table->setItemWidget(item, 5, deadband);
        auto *threshold = new QDoubleSpinBox;
        threshold->setRange(-1, 1);
        threshold->setSpecialValueText("Off");
        threshold->setDecimals(3);
        threshold->setValue(settings.value(settingsKey + "/threshold", -1).toDouble());
        connect(threshold, &QDoubleSpinBox::valueChanged, this, [settingsKey](double value) {
            QSettings().setValue(settingsKey + "/threshold", value);
        });
        table->setItemWidget(item, 6, threshold);
        return {item, meter, smooth, deadband, threshold};
    }

    void updateSignals(const QJsonObject &frame)
    {
        const auto values = signalsFor(frame);
        QList<Destination> targets;
        const bool valid = parseDestinations(destinations->text(), targets);
        const bool armed = send->isChecked() && valid;
        const double dt = std::clamp(clock.restart() / 1000.0, .001, .5);
        for (auto i = values.begin(); i != values.end(); ++i)
            if (!rows.contains(i.key())) rows.insert(i.key(), makeRow(i.key()));
        int messages = 0, errors = 0;
        for (auto i = rows.begin(); i != rows.end(); ++i) {
            const bool available = values.contains(i.key());
            const double raw = values.value(i.key());
            const double tau = i->smooth->value() / 1000.0;
            i->filtered = !i->initialized || tau == 0 ? raw : i->filtered + (raw - i->filtered) * -std::expm1(-dt / tau);
            i->initialized = available;
            const double output = i->threshold->value() < 0 ? i->filtered : double(i->filtered >= i->threshold->value());
            i->item->setText(1, available ? QString::number(raw, 'f', 3) : QStringLiteral("—"));
            i->meter->setValue(qRound(1000 * std::clamp(output, 0.0, 1.0)));
            i->meter->setFormat(available ? QString::number(output, 'f', 3) : QStringLiteral("—"));
            if (!armed || i->item->checkState(3) != Qt::Checked) { i->sent = false; continue; }
            if (!available) {
                if (!i->sent) continue;
            } else if (i->sent && i->deadband->value() > 0 && std::abs(output - i->lastSent) < i->deadband->value()) continue;
            const double outgoing = available ? output : 0;
            const auto packet = osc::encode({i.key(), "f", {outgoing}});
            bool delivered = true;
            for (const auto &target : targets) {
                if (udp.writeDatagram(packet, target.ip, target.port) < 0) { ++errors; delivered = false; }
                else ++messages;
            }
            if (delivered) { i->sent = available; i->lastSent = outgoing; }
        }
        QString label = send->isChecked() && !valid ? "Invalid OSC destination" :
                        QString("%1 live signals · %2 OSC datagrams").arg(values.size()).arg(messages);
        for (const auto &key : frame.keys())
            if (key == "error" || key.endsWith("_error")) label += " · " + frame.value(key).toString();
        if (errors) label += QString(" · %1 send errors").arg(errors);
        status->setText(label);
    }

    QComboBox *cameraChoice, *mode;
    QLineEdit *destinations;
    QCheckBox *send;
    QLabel *picture, *status;
    QTreeWidget *table;
    QMap<QString, Row> rows;
    QString query;
    QMediaCaptureSession capture;
    QMediaPlayer player;
    QVideoSink cameraSink, fileSink;
    std::unique_ptr<QCamera> camera;
    QProcess worker;
    QUdpSocket udp;
    QTimer timer;
    QElapsedTimer clock;
    QImage latest, submitted;
    int submittedMask = 0;
    bool ready = false, busy = false, active = false, running = false;
};
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("MP_OSC");
    app.setOrganizationName("PixelStereo");
    if (app.arguments().contains("--check-mediapipe")) {
        const auto root = runtimeFolder();
        QProcess worker;
        worker.start(pythonExecutable(root), {"-I", "-u", "-c", QString::fromUtf8(workerScript), root});
        if (!worker.waitForStarted()) return 1;
        worker.write(QByteArray("128 128 4110\n") + QByteArray(128 * 128 * 4, '\0'));
        worker.closeWriteChannel();
        if (!worker.waitForFinished(60000)) return 2;
        const auto lines = worker.readAllStandardOutput().trimmed().split('\n');
        if (lines.size() != 2 || !QJsonDocument::fromJson(lines[0]).object().value("ready").toBool()) return 3;
        const auto frame = QJsonDocument::fromJson(lines[1]).object();
        if (!frame.contains("face") || !frame.contains("hands") || !frame.contains("pose") || !frame.contains("preview_jpeg")) {
            qCritical().noquote() << lines[1];
            return 4;
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
        qInfo("Packaged inference, annotations and two OSC destinations passed");
        return 0;
    }
    Window window;
    window.show();
    return app.exec();
}
