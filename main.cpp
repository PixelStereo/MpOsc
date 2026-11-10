#include "OscCodec.h"
#include "SliderField.h"
#include "Worker.h"

#include <QtWidgets>
#include <QSaveFile>
#include <QTemporaryDir>
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
#include <QtMath>
#include <QPermissions>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QCameraFormat>
#include <memory>
#include <utility>
#include <algorithm>
#include <cmath>

namespace {
QIcon signalPinIcon(bool pinned)
{
    QPixmap icon(32, 32);
    icon.setDevicePixelRatio(2);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(8, 8);
    painter.rotate(pinned ? 0 : 38);
    const QColor color(pinned ? "#8394b3" : "#a5a5af");
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

class SignalItem final : public QTreeWidgetItem
{
public:
    SignalItem(QTreeWidget *tree, const QString &address) : QTreeWidgetItem(tree)
    {
        setData(0, Qt::UserRole, address);
        setData(0, Qt::AccessibleTextRole, address);
        setSizeHint(0, QSize(0, 26));
    }

    bool operator<(const QTreeWidgetItem &other) const override
    {
        return data(0, Qt::UserRole).toString() < other.data(0, Qt::UserRole).toString();
    }
};

class VideoPreview final : public QWidget
{
public:
    using QWidget::QWidget;
    QRectF roi{0, 0, 1, 1};
    bool editRoi = false;
    void showFrame(const QImage &frame) { image = frame; update(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#1e1e21"));
        if (image.isNull()) {
            painter.setPen(QColor("#e1e1e4"));
            painter.drawText(rect(), Qt::AlignCenter, "Choose a camera or a video");
            return;
        }
        const auto area = imageRect();
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(area, image);
        if (editRoi || roi != QRectF(0, 0, 1, 1)) {
            const QRectF selection(area.x() + roi.x() * area.width(), area.y() + roi.y() * area.height(),
                                   roi.width() * area.width(), roi.height() * area.height());
            QPainterPath shade;
            shade.addRect(area);
            shade.addRect(selection);
            painter.fillPath(shade, QColor(0, 0, 0, 90));
            painter.setPen(QPen(QColor("#8394b3"), 2));
            painter.drawRect(selection.adjusted(1, 1, -1, -1));
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (!editRoi || event->button() != Qt::LeftButton || !imageRect().contains(event->position())) return;
        anchor = imagePoint(event->position());
        dragging = true;
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (dragging) select(event->position());
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (!dragging || event->button() != Qt::LeftButton) return;
        select(event->position());
        dragging = false;
    }

private:
    QRectF imageRect() const
    {
        if (image.isNull()) return {};
        const QSize size = image.size().scaled(this->size(), Qt::KeepAspectRatio);
        return QRectF(QPointF((width() - size.width()) / 2., (height() - size.height()) / 2.), size);
    }
    QPointF imagePoint(const QPointF &point) const
    {
        const auto area = imageRect();
        return {std::clamp((point.x() - area.x()) / std::max(1., area.width()), 0., 1.),
                std::clamp((point.y() - area.y()) / std::max(1., area.height()), 0., 1.)};
    }
    void select(const QPointF &point)
    {
        const auto selection = QRectF(anchor, imagePoint(point)).normalized();
        if (selection.width() * image.width() >= 2 && selection.height() * image.height() >= 2) {
            roi = selection;
            update();
        }
    }
    QImage image;
    QPointF anchor;
    bool dragging = false;
};

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

class Window : public QWidget {
public:
    Window()
    {
        setWindowTitle("MP_OSC");
        const QSettings settings;
        resize(1120, 690);
        restoreGeometry(settings.value("window/geometry").toByteArray());
        auto *layout = new QVBoxLayout(this);
        auto *source = new QHBoxLayout;
        cameraChoice = new QComboBox;
        for (const auto &device : QMediaDevices::videoInputs()) cameraChoice->addItem(device.description(), device.id());
        const int previousCamera = cameraChoice->findData(settings.value("video/cameraId").toByteArray());
        if (previousCamera >= 0) cameraChoice->setCurrentIndex(previousCamera);
        auto *startCamera = new QPushButton("Camera");
        startCamera->setEnabled(cameraChoice->count() != 0);
        auto *open = new QPushButton("Open video…");
        auto *stop = new QPushButton("Stop");
        faceTracking = new QPushButton("Face");
        handTracking = new QPushButton("Hands");
        bodyTracking = new QPushButton("Body");
        const auto styleToggle = [](QPushButton *button) {
            button->setCheckable(true);
            button->setStyleSheet(QStringLiteral(
                "QPushButton { background:#343439; color:#e1e1e4; border:1px solid #505057;"
                " border-radius:6px; padding:5px 9px; }"
                "QPushButton:hover { border-color:#9aabc6; }"
                "QPushButton:checked { background:#8394b3; color:#1a1a1d;"
                " border-color:#9aabc6; font-weight:600; }"));
        };
        for (auto *button : {faceTracking, handTracking, bodyTracking}) styleToggle(button);
        const int previousTracking = settings.value("video/trackingMask", 4).toInt();
        faceTracking->setChecked(previousTracking & 4);
        handTracking->setChecked(previousTracking & 2);
        bodyTracking->setChecked(previousTracking & 8);
        for (auto *w : QList<QWidget *>{cameraChoice, startCamera, open, stop, faceTracking, handTracking, bodyTracking})
            source->addWidget(w);
        auto *about = new QPushButton("About");
        about->setIcon(QApplication::windowIcon());
        source->addWidget(about);
        connect(about, &QPushButton::clicked, this, [this] {
            QMessageBox dialog(this);
            dialog.setWindowTitle("About MP_OSC");
            dialog.setIconPixmap(QApplication::windowIcon().pixmap(96, 96));
            dialog.setText(QStringLiteral("<h2>MP_OSC</h2><p>Motion Processing OSC</p>"));
            dialog.setInformativeText("Turn movement into live OSC controls.\nBuilt by Pixel Stereo.");
            dialog.setStandardButtons(QMessageBox::Ok);
            dialog.exec();
        });
        layout->addLayout(source);

        auto *videoBar = new QHBoxLayout;
        auto *savePreset = new QPushButton("Save .mposc…");
        auto *loadPreset = new QPushButton("Open .mposc…");
        videoBar->addWidget(savePreset);
        videoBar->addWidget(loadPreset);
        connect(savePreset, &QPushButton::clicked, this, [this] { saveConfiguration(); });
        connect(loadPreset, &QPushButton::clicked, this, [this] { loadConfiguration(); });
        auto *roi = new QPushButton("Draw ROI");
        roi->setCheckable(true);
        auto *resetRoi = new QPushButton("Reset ROI");
        videoBar->addWidget(roi);
        videoBar->addWidget(resetRoi);
        videoBar->addStretch();
        resolution = new QLabel("No video frame");
        videoBar->addWidget(resolution);
        layout->addLayout(videoBar);

        auto *oscBar = new QHBoxLayout;
        destinations = new QLineEdit(settings.value("destinations", "127.0.0.1:5000").toString());
        destinations->setPlaceholderText("127.0.0.1:5000 10.0.0.2:9999");
        send = new QPushButton("Send OSC");
        styleToggle(send);
        send->setChecked(settings.value("osc/send", false).toBool());
        oscBar->addWidget(new QLabel("Destinations"));
        oscBar->addWidget(destinations, 1);
        oscBar->addWidget(send);
        layout->addLayout(oscBar);
        split = new QSplitter;
        picture = new VideoPreview;
        const QRectF fullFrame(0, 0, 1, 1);
        const auto previousRoi = settings.value("video/roi", fullFrame).toRectF().intersected(fullFrame);
        if (!previousRoi.isEmpty()) picture->roi = previousRoi;
        connect(roi, &QPushButton::toggled, picture, [this](bool enabled) {
            picture->editRoi = enabled;
            picture->setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
            picture->update();
        });
        connect(resetRoi, &QPushButton::clicked, picture, [this] {
            picture->roi = QRectF(0, 0, 1, 1);
            picture->update();
        });
        picture->setMinimumWidth(250);
        picture->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        split->addWidget(picture);
        auto *right = new QWidget;
        auto *controls = new QVBoxLayout(right);
        search = new QLineEdit;
        search->setPlaceholderText("Filter signals by name");
        auto *signalActions = new QHBoxLayout;
        auto *enableAll = new QPushButton("Enable all");
        auto *disableAll = new QPushButton("Disable all");
        signalActions->addWidget(search, 1);
        signalActions->addWidget(enableAll);
        signalActions->addWidget(disableAll);
        controls->addLayout(signalActions);
        connect(enableAll, &QPushButton::clicked, this, [this] {
            for (auto &row : rows) row.signal->setChecked(true);
        });
        connect(disableAll, &QPushButton::clicked, this, [this] {
            for (auto &row : rows) row.signal->setChecked(false);
        });
        const auto makeTable = [] {
            auto *view = new QTreeWidget;
            view->setRootIsDecorated(false);
            view->setSelectionMode(QAbstractItemView::NoSelection);
            view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
            view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
            view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
            view->setHeaderLabels({"Signal / OSC address", "Raw", "Invert", "Attack", "Release", "Value"});
            view->header()->setStretchLastSection(false);
            view->setColumnWidth(0, 195);
            view->setColumnWidth(1, 75);
            view->setColumnWidth(2, 56);
            for (int column : {3, 4}) view->setColumnWidth(column, 104);
            view->setColumnWidth(5, 85);
            view->header()->setStretchLastSection(true);
            view->setSortingEnabled(true);
            view->sortItems(0, Qt::AscendingOrder);
            view->header()->setSortIndicatorShown(false);
            view->header()->setSectionsClickable(false);
            return view;
        };
        pinnedTable = makeTable();
        table = makeTable();
        pinnedTable->hide();
        controls->addWidget(pinnedTable);
        controls->addWidget(table, 1);
        for (auto *view : {table, pinnedTable}) {
            auto *other = view == table ? pinnedTable : table;
            connect(view->horizontalScrollBar(), &QScrollBar::valueChanged,
                    other->horizontalScrollBar(), &QScrollBar::setValue);
            connect(view->header(), &QHeaderView::sectionResized, other, [other](int column, int, int width) {
                if (column != 5 && other->columnWidth(column) != width) other->setColumnWidth(column, width);
            });
        }
        split->addWidget(right);
        if (!split->restoreState(settings.value("window/split").toByteArray()))
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
            if (latest.isNull()) return;
            if (latest.size() != sourceSize) {
                sourceSize = latest.size();
                resolution->setText(QStringLiteral("%1 × %2 px (%3 pixels)")
                    .arg(sourceSize.width()).arg(sourceSize.height())
                    .arg(locale().toString(qint64(sourceSize.width()) * sourceSize.height())));
            }
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
                    QCameraFormat preferred;
                    for (const auto &format : device.videoFormats()) {
                        const auto size = format.resolution();
                        if (size.width() <= size.height()) continue;
                        const auto current = preferred.resolution();
                        if (preferred.isNull() || qint64(size.width()) * size.height() > qint64(current.width()) * current.height())
                            preferred = format;
                    }
                    if (!preferred.isNull()) camera->setCameraFormat(preferred);
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
            QSettings settings;
            const auto path = QFileDialog::getOpenFileName(this, "Open video", settings.value("video/lastDirectory").toString());
            if (path.isEmpty()) return;
            settings.setValue("video/lastDirectory", QFileInfo(path).absolutePath());
            stopSource();
            running = true;
            player.setSource(QUrl::fromLocalFile(path));
            player.play();
        });
        connect(stop, &QPushButton::clicked, this, [this] { stopSource(); picture->showFrame({}); });
        connect(&player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &error) { status->setText(error); });
        connect(search, &QLineEdit::textChanged, this, [this](const QString &text) {
            query = text;
            for (auto i = rows.begin(); i != rows.end(); ++i)
                if (i->item->treeWidget() == table) i->item->setHidden(!i.key().contains(query, Qt::CaseInsensitive));
        });
        connect(destinations, &QLineEdit::editingFinished, this, [this] {
            QSettings().setValue("destinations", destinations->text());
            for (auto &row : rows) row.sent = false;
        });
        connect(send, &QPushButton::toggled, this, [this] {
            for (auto &row : rows) {
                row.sent = false;
                row.meter->setEnabled(send->isChecked() && row.signal->isChecked());
            }
        });
        connect(&worker, &QProcess::readyReadStandardOutput, this, [this] {
            while (worker.canReadLine()) {
                const auto result = QJsonDocument::fromJson(worker.readLine()).object();
                if (result.value("ready").toBool()) { ready = true; status->setText("MediaPipe ready"); continue; }
                busy = false;
                for (auto entry = result.begin(); entry != result.end(); ++entry) {
                    if (entry.key() == "error" || entry.key().endsWith("_error"))
                        status->setText(entry.value().toString());
                }
                if (!active) continue;
                const QImage annotated = QImage::fromData(QByteArray::fromBase64(result.value("preview_jpeg").toString().toLatin1()));
                if (!annotated.isNull()) {
                    QPainter painter(&submittedSource);
                    painter.drawImage(submittedRoi, annotated);
                }
                showFrame(submittedSource);
                if (submittedMask == trackingMask()) updateSignals(result);
            }
        });
        connect(&worker, &QProcess::errorOccurred, this, [this] { status->setText(worker.errorString()); });
        connect(&worker, &QProcess::readyReadStandardError, this, [this] {
            workerLog = (workerLog + worker.readAllStandardError()).right(4096);
        });
        connect(&worker, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this](int code, QProcess::ExitStatus exitStatus) {
            ready = busy = false;
            if (shuttingDown) return;
            status->setText(exitStatus == QProcess::CrashExit
                ? QStringLiteral("MediaPipe crashed (%1)").arg(code)
                : QStringLiteral("MediaPipe exited (%1)").arg(code));
            status->setToolTip(QString::fromUtf8(workerLog));
            if (!workerRetried) {
                workerRetried = true;
                QTimer::singleShot(250, this, [this] { startWorker(); });
            }
        });
        startWorker();
        clock.start();
        connect(&timer, &QTimer::timeout, this, [this] {
            if (!ready || busy || latest.isNull()) return;
            submittedSource = std::exchange(latest, QImage()).convertToFormat(QImage::Format_RGBA8888);
            const auto roi = picture->roi;
            const int width = submittedSource.width(), height = submittedSource.height();
            submittedRoi = QRectF(roi.x() * width, roi.y() * height, roi.width() * width, roi.height() * height)
                               .toAlignedRect().intersected(submittedSource.rect());
            submitted = submittedRoi == submittedSource.rect() ? submittedSource : submittedSource.copy(submittedRoi);
            submittedMask = trackingMask();
            QByteArray packet = QByteArray::number(submitted.width()) + ' ' + QByteArray::number(submitted.height()) +
                                ' ' + QByteArray::number(submittedMask | 4096) + '\n';
            for (int y = 0; y < submitted.height(); ++y)
                packet.append(reinterpret_cast<const char *>(submitted.constScanLine(y)), submitted.width() * 4);
            active = busy = true;
            worker.write(packet);
        });
        search->setText(settings.value("window/search").toString());
        restoreSignalRows(settings);
        timer.start(33);
    }

    void storeSettings(QSettings &settings)
    {
        settings.setValue("window/search", search->text());
        settings.setValue("window/geometry", saveGeometry());
        settings.setValue("window/split", split->saveState());
        if (cameraChoice->currentIndex() >= 0)
            settings.setValue("video/cameraId", cameraChoice->currentData());
        settings.setValue("video/trackingMask", trackingMask());
        settings.setValue("video/roi", picture->roi);
        settings.setValue("osc/send", send->isChecked());
        settings.setValue("destinations", destinations->text());
    }

    ~Window() override
    {
        QSettings settings;
        storeSettings(settings);
        shuttingDown = true;
        stopSource();
        worker.closeWriteChannel();
        if (worker.state() != QProcess::NotRunning && !worker.waitForFinished(300)) {
            worker.kill();
            worker.waitForFinished(1000);
        }
    }

private:
    void restoreSignalRows(const QSettings &settings)
    {
        for (const auto &key : settings.allKeys()) {
            if (!key.startsWith("signals/")) continue;
            const auto end = key.lastIndexOf('/');
            if (end <= 7) continue;
            const auto address = key.mid(7, end - 7);
            if (!rows.contains(address)) rows.insert(address, makeRow(address));
        }
    }

    void saveConfiguration()
    {
        auto path = QFileDialog::getSaveFileName(this, "Save MP_OSC configuration", {}, "MP_OSC (*.mposc)");
        if (path.isEmpty()) return;
        if (!path.endsWith(".mposc", Qt::CaseInsensitive)) path += ".mposc";
        QTemporaryDir staging;
        if (!staging.isValid()) {
            QMessageBox::warning(this, "MP_OSC", "Could not create a temporary configuration file.");
            return;
        }
        QSettings local;
        storeSettings(local);
        QSettings snapshot(staging.filePath("snapshot.ini"), QSettings::IniFormat);
        for (const auto &key : local.allKeys()) snapshot.setValue(key, local.value(key));
        snapshot.setValue("formatVersion", 1);
        snapshot.sync();
        QFile data(snapshot.fileName());
        QSaveFile file(path);
        if (snapshot.status() != QSettings::NoError || !data.open(QIODevice::ReadOnly) || !file.open(QIODevice::WriteOnly)) {
            QMessageBox::warning(this, "MP_OSC", "Could not save the configuration.");
            return;
        }
        const auto bytes = data.readAll();
        if (data.error() != QFileDevice::NoError || file.write(bytes) != bytes.size() || !file.commit())
            QMessageBox::warning(this, "MP_OSC", "Could not save the configuration.");
    }

    void loadConfiguration()
    {
        const auto path = QFileDialog::getOpenFileName(this, "Open MP_OSC configuration", {}, "MP_OSC (*.mposc)");
        if (path.isEmpty()) return;
        const QSettings snapshot(path, QSettings::IniFormat);
        if (snapshot.value("formatVersion").toInt() != 1 || snapshot.status() != QSettings::NoError) {
            QMessageBox::warning(this, "MP_OSC", "This file is not a supported MP_OSC configuration.");
            return;
        }
        stopSource();
        picture->showFrame({});
        resolution->setText("No video frame");
        sourceSize = {};
        QSettings local;
        for (const auto &group : {"signals", "video", "window", "osc"}) local.remove(group);
        for (const auto &key : snapshot.allKeys()) {
            if (key != "formatVersion") local.setValue(key, snapshot.value(key));
        }
        local.sync();
        rows.clear();
        table->clear();
        pinnedTable->clear();
        resizePinnedTable();
        restoreGeometry(local.value("window/geometry").toByteArray());
        split->restoreState(local.value("window/split").toByteArray());
        const int camera = cameraChoice->findData(local.value("video/cameraId").toByteArray());
        cameraChoice->setCurrentIndex(camera >= 0 ? camera : (cameraChoice->count() > 0 ? 0 : -1));
        const int mask = local.value("video/trackingMask", 4).toInt();
        faceTracking->setChecked(mask & 4);
        handTracking->setChecked(mask & 2);
        bodyTracking->setChecked(mask & 8);
        const QRectF fullFrame(0, 0, 1, 1);
        const auto selection = local.value("video/roi", fullFrame).toRectF().intersected(fullFrame);
        picture->roi = selection.isEmpty() ? fullFrame : selection;
        destinations->setText(local.value("destinations", "127.0.0.1:5000").toString());
        send->setChecked(local.value("osc/send", false).toBool());
        search->setText(local.value("window/search").toString());
        restoreSignalRows(local);
        status->setText("Configuration loaded");
    }

    void startWorker()
    {
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("MPLBACKEND", "Agg");
        if (workerRetried || !environment.contains("NYXMAP_MEDIAPIPE_DELEGATE"))
            environment.insert("NYXMAP_MEDIAPIPE_DELEGATE", "cpu");
        worker.setProcessEnvironment(environment);
        const auto root = runtimeFolder();
        worker.start(pythonExecutable(root), {"-I", "-u", "-c", QString::fromUtf8(workerScript), root});
    }

    struct Row {
        QTreeWidgetItem *item;
        QProgressBar *meter;
        QLabel *raw;
        QCheckBox *invert;
        QPushButton *signal;
        SliderField *attack, *release;
        double filtered = 0;
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
        picture->showFrame(image);
    }

    void resizePinnedTable()
    {
        const int count = pinnedTable->topLevelItemCount();
        pinnedTable->setVisible(count > 0);
        table->setHeaderHidden(count > 0);
        if (count > 0) {
            const int rowHeight = std::max(26, pinnedTable->visualItemRect(pinnedTable->topLevelItem(0)).height());
            pinnedTable->setFixedHeight(pinnedTable->header()->sizeHint().height() +
                                       std::min(count, 6) * rowHeight + 2 * pinnedTable->frameWidth() +
                                       pinnedTable->horizontalScrollBar()->sizeHint().height());
        }
    }

    void moveSignal(const QString &address, bool pinned)
    {
        auto row = rows.find(address);
        if (row == rows.end() || (row->item->treeWidget() == pinnedTable) == pinned) return;
        const auto previous = *row;
        const int value = previous.meter->value();
        const auto format = previous.meter->format();
        const auto raw = previous.raw->text();
        const double attack = previous.attack->value();
        const double release = previous.release->value();
        delete previous.item;
        *row = makeRow(address);
        row->filtered = previous.filtered;
        row->initialized = previous.initialized;
        row->sent = previous.sent;
        row->attack->setValue(attack);
        row->release->setValue(release);
        row->raw->setText(raw);
        row->meter->setFormat(format);
        row->meter->setValue(value);
        resizePinnedTable();
    }

    Row makeRow(const QString &address)
    {
        const auto settingsKey = QStringLiteral("signals") + address;
        QSettings settings;
        const bool pinned = settings.value(settingsKey + "/pinned", false).toBool();
        auto *view = pinned ? pinnedTable : table;
        auto *item = new SignalItem(view, address);
        item->setHidden(!pinned && !address.contains(query, Qt::CaseInsensitive));
        auto *signal = new QPushButton(address);
        signal->setCheckable(true);
        signal->setChecked(settings.value(settingsKey + "/send", false).toBool());
        signal->setStyleSheet(QStringLiteral(
            "QPushButton { background:transparent; color:#e1e1e4; text-align:left;"
            " border:1px solid transparent; border-radius:5px; padding:3px 6px; }"
            "QPushButton:hover { background:#343439; }"));
        auto *nameCell = new QFrame;
        const auto updateOutline = [nameCell](bool enabled) {
            nameCell->setStyleSheet(enabled
                ? QStringLiteral("QFrame { border:1px solid #8394b3; border-radius:5px; }")
                : QStringLiteral("QFrame { border:1px solid transparent; border-radius:5px; }"));
        };
        updateOutline(signal->isChecked());
        connect(signal, &QPushButton::toggled, nameCell, updateOutline);
        auto *nameLayout = new QHBoxLayout(nameCell);
        nameLayout->setContentsMargins(1, 1, 1, 1);
        nameLayout->setSpacing(4);
        auto *pin = new QToolButton;
        pin->setFixedSize(24, 24);
        pin->setIconSize(QSize(16, 16));
        pin->setCursor(Qt::PointingHandCursor);
        pin->setCheckable(true);
        pin->setAutoRaise(true);
        pin->setStyleSheet(QStringLiteral(
            "QToolButton { border:0; border-radius:4px; }"
            "QToolButton:hover { background:#343439; }"
            "QToolButton:checked { background:#282e3b; }"));
        pin->setChecked(pinned);
        static const QIcon unpinnedIcon = signalPinIcon(false);
        static const QIcon pinnedIcon = signalPinIcon(true);
        pin->setIcon(pinned ? pinnedIcon : unpinnedIcon);
        pin->setToolTip(pinned ? "Unpin signal" : "Pin signal");
        pin->setAccessibleName(pin->toolTip());
        connect(pin, &QToolButton::toggled, this, [this, address, settingsKey](bool enabled) {
            QSettings().setValue(settingsKey + "/pinned", enabled);
            moveSignal(address, enabled);
        }, Qt::QueuedConnection);
        signal->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        signal->setToolTip(address);
        nameLayout->addWidget(pin, 0, Qt::AlignVCenter);
        nameLayout->addWidget(signal, 1);
        view->setItemWidget(item, 0, nameCell);

        auto *raw = new QLabel(QStringLiteral("—"));
        raw->setAlignment(Qt::AlignCenter);
        view->setItemWidget(item, 1, raw);

        auto *invert = new QCheckBox;
        invert->setAccessibleName("Invert " + address);
        invert->setToolTip("Invert before Attack and Release");
        invert->setChecked(settings.value(settingsKey + "/invert", false).toBool());
        auto *invertCell = new QWidget;
        auto *invertLayout = new QHBoxLayout(invertCell);
        invertLayout->setContentsMargins(0, 0, 0, 0);
        invertLayout->addWidget(invert, 0, Qt::AlignCenter);
        view->setItemWidget(item, 2, invertCell);
        connect(invert, &QCheckBox::toggled, this, [settingsKey](bool enabled) {
            QSettings().setValue(settingsKey + "/invert", enabled);
        });

        auto *meter = new QProgressBar;
        if (address.startsWith("/face/head/")) {
            const int limit = address.endsWith("/pitch") ? 90 : 180;
            meter->setRange(-limit, limit);
        } else {
            meter->setRange(0, 1000);
        }
        meter->setEnabled(send->isChecked() && signal->isChecked());
        meter->setStyleSheet(QStringLiteral(
            "QProgressBar { background:#1e1e21; color:#e1e1e4; border:1px solid #4b4b51;"
            " border-radius:4px; text-align:center; }"
            "QProgressBar::chunk { background:#8394b3; border-radius:3px; }"
            "QProgressBar:disabled { color:#99999f; }"
            "QProgressBar::chunk:disabled { background:#45454d; }"));
        view->setItemWidget(item, 5, meter);
        connect(signal, &QPushButton::toggled, this, [this, address, settingsKey, meter](bool enabled) {
            rows[address].sent = false;
            meter->setEnabled(send->isChecked() && enabled);
            QSettings().setValue(settingsKey + "/send", enabled);
        });

        const auto addTimeField = [&](int column, const QString &name) {
            const auto key = settingsKey + name;
            auto *field = new SliderField;
            field->setDecimals(3);
            field->setRange(0, 2);
            field->setTypedRange(0, 60);
            field->setSingleStep(.01);
            field->setSuffix(" s");
            field->setValue(settings.value(key, settings.value(settingsKey + "/smooth", 0).toDouble() / 1000).toDouble());
            connect(field, &SliderField::editingFinished, this, [key](double value) {
                QSettings().setValue(key, value);
            });
            view->setItemWidget(item, column, field);
            return field;
        };
        auto row = Row{item, meter, raw, invert, signal, addTimeField(3, "/attack"), addTimeField(4, "/release")};
        if (pinned) resizePinnedTable();
        return row;
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
            const bool angle = i->meter->minimum() < 0;
            const bool wraps = angle && i->meter->maximum() == 180;
            const auto formatValue = [angle, available](double value) {
                if (!available) return QStringLiteral("—");
                auto text = QString::number(value, 'f', angle ? 1 : 3);
                if (angle) text += QStringLiteral("°");
                return text;
            };
            i->raw->setText(formatValue(raw));
            const double input = i->invert->isChecked() ? invertedSignal(i.key(), raw) : raw;
            const double difference = input - i->filtered;
            const double delta = wraps ? std::remainder(difference, 360.0) : difference;
            const double tau = delta > 0 ? i->attack->value() : i->release->value();
            i->filtered = !i->initialized || tau == 0 ? input : i->filtered + delta * -std::expm1(-dt / tau);
            if (wraps) i->filtered = std::remainder(i->filtered, 360.0);
            i->initialized = available;
            const double output = i->filtered;
            i->meter->setValue(qRound(angle
                ? std::clamp(output, double(i->meter->minimum()), double(i->meter->maximum()))
                : 1000 * std::clamp(output, 0.0, 1.0)));
            i->meter->setFormat(formatValue(output));
            if (!armed || !i->signal->isChecked()) { i->sent = false; continue; }
            if (!available && !i->sent) continue;
            const double outgoing = available ? output : 0;
            const auto packet = osc::encode({i.key(), "f", {outgoing}});
            bool delivered = true;
            for (const auto &target : targets) {
                if (udp.writeDatagram(packet, target.ip, target.port) < 0) { ++errors; delivered = false; }
                else ++messages;
            }
            if (delivered) i->sent = available;
        }
        QString label = send->isChecked() && !valid ? "Invalid OSC destination" :
                        QString("%1 live signals · %2 OSC datagrams").arg(values.size()).arg(messages);
        for (const auto &key : frame.keys())
            if (key == "error" || key.endsWith("_error")) label += " · " + frame.value(key).toString();
        if (errors) label += QString(" · %1 send errors").arg(errors);
        status->setText(label);
    }

    int trackingMask() const
    {
        return (faceTracking->isChecked() ? 4 : 0)
             | (handTracking->isChecked() ? 2 : 0)
             | (bodyTracking->isChecked() ? 8 : 0);
    }

    QComboBox *cameraChoice;
    QPushButton *faceTracking, *handTracking, *bodyTracking;
    QLabel *resolution;
    QSize sourceSize;
    QByteArray workerLog;
    bool workerRetried = false, shuttingDown = false;
    QLineEdit *destinations, *search;
    QPushButton *send;
    QSplitter *split;
    VideoPreview *picture;
    QLabel *status;
    QTreeWidget *table, *pinnedTable;
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
    QImage latest, submitted, submittedSource;
    QRect submittedRoi;
    int submittedMask = 0;
    bool ready = false, busy = false, active = false, running = false;
};
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("MP_OSC");
    app.setWindowIcon(QIcon(":/assets/MP_OSC.png"));
    app.setOrganizationName("PixelStereo");
    auto palette = app.palette();
    palette.setColor(QPalette::Highlight, QColor("#8394b3"));
    palette.setColor(QPalette::HighlightedText, QColor("#1a1a1d"));
    app.setPalette(palette);
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
        qInfo("Packaged inference, annotations, head rotation, inversion and two OSC destinations passed");
        return 0;
    }
    Window window;
    window.show();
    return app.exec();
}
