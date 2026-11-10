#include "OscCodec.h"
#include "SliderField.h"
#include "Worker.h"
#include "MpOscVersion.h"
#include "SharedInput.h"
#include "TrackingOverlay.h"

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
#include <QStyleHints>
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
void applyInterfacePalette(QApplication &app, Qt::ColorScheme scheme)
{
    const bool dark = scheme == Qt::ColorScheme::Dark;
    const QColor text(dark ? "#f0f4fa" : "#182234");
    const QColor muted(dark ? "#b7c4d6" : "#49586c");
    const QColor accent(dark ? "#435a7d" : "#a9bfdf");
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(dark ? "#252c37" : "#f2f5f9"));
    palette.setColor(QPalette::Base, QColor(dark ? "#1d2430" : "#ffffff"));
    palette.setColor(QPalette::AlternateBase, QColor(dark ? "#303a49" : "#e9edf4"));
    palette.setColor(QPalette::Button, QColor(dark ? "#303947" : "#ffffff"));
    palette.setColor(QPalette::Mid, QColor(dark ? "#7789a3" : "#64758b"));
    palette.setColor(QPalette::Light, QColor(dark ? "#4b586c" : "#ffffff"));
    palette.setColor(QPalette::Midlight, QColor(dark ? "#3a4759" : "#e7ecf4"));
    palette.setColor(QPalette::Dark, QColor(dark ? "#121822" : "#a6b3c6"));
    palette.setColor(QPalette::Shadow, QColor(dark ? "#0b1018" : "#73849b"));
    palette.setColor(QPalette::Highlight, accent);
    palette.setColor(QPalette::Link, QColor(dark ? "#adc8ef" : "#315a89"));
    palette.setColor(QPalette::LinkVisited, QColor(dark ? "#cdb8ef" : "#624587"));
    palette.setColor(QPalette::ToolTipBase, palette.color(QPalette::Base));
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::PlaceholderText, muted);
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::HighlightedText}) {
        palette.setColor(role, text);
        palette.setColor(QPalette::Disabled, role, muted);
    }
    palette.setColor(QPalette::Disabled, QPalette::Highlight, palette.color(QPalette::AlternateBase));
    app.setPalette(palette);

    QString style = QStringLiteral(R"(
QGroupBox#section { border:1px solid @border; border-radius:8px; margin-top:9px; }
QGroupBox#section::title { color:@text; subcontrol-origin:margin; left:11px; padding:0 5px; font-weight:600; }
QPushButton#toggle { background:@button; color:@text; border:1px solid @border; border-radius:6px; padding:5px 9px; }
QPushButton#toggle:hover, QPushButton#toggle:focus { border-color:@focus; }
QPushButton#toggle:checked { background:@fill; color:@text; border-color:@focus; font-weight:600; }
QPushButton#toggle:disabled { color:@muted; }
QPushButton#signalName { background:transparent; color:@text; text-align:left;
    border:1px solid transparent; border-radius:5px; padding:3px 6px; }
QPushButton#signalName:hover, QPushButton#signalName:focus { background:@alternate; }
QFrame#signalCell { border:1px solid transparent; border-radius:5px; }
QFrame#signalCell[oscEnabled="true"] { border-color:@focus; }
QToolButton#signalPin { border:1px solid transparent; border-radius:4px; }
QToolButton#signalPin:hover, QToolButton#signalPin:checked { background:@alternate; }
QToolButton#signalPin:focus { border-color:@focus; }
QProgressBar#signalValue { background:@base; color:@text; border:1px solid @border; border-radius:4px; text-align:center; }
QProgressBar#signalValue::chunk { background:@fill; border-radius:3px; }
QProgressBar#signalValue:disabled { color:@muted; }
QProgressBar#signalValue::chunk:disabled { background:@alternate; }
)");
    style.replace("@text", text.name()).replace("@muted", muted.name()).replace("@fill", accent.name())
         .replace("@button", palette.color(QPalette::Button).name()).replace("@base", palette.color(QPalette::Base).name())
         .replace("@alternate", palette.color(QPalette::AlternateBase).name()).replace("@border", palette.color(QPalette::Mid).name())
         .replace("@focus", palette.color(QPalette::Link).name());
    app.setStyleSheet(style);
}

QIcon signalPinIcon(bool pinned, const QColor &color)
{
    QPixmap icon(32, 32);
    icon.setDevicePixelRatio(2);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(8, 8);
    painter.rotate(pinned ? 0 : 38);
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

class PinButton final : public QToolButton {
public:
    PinButton()
    {
        setCheckable(true);
        setFixedSize(24, 24);
        setIconSize(QSize(16, 16));
        setCursor(Qt::PointingHandCursor);
        setObjectName("signalPin");
        connect(this, &QToolButton::toggled, this, [this] { refreshIcon(); });
        refreshIcon();
    }

protected:
    void changeEvent(QEvent *event) override
    {
        QToolButton::changeEvent(event);
        if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) refreshIcon();
    }

private:
    void refreshIcon() { setIcon(signalPinIcon(isChecked(), QApplication::palette().color(QPalette::Link))); }
};

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
    bool editRoi = false, mirror = false, showLandmarks = true;
    bool isDrawing() const { return dragging; }
    void showFrame(const QImage &frame, const QJsonObject &tracking = {}, const QRectF &trackedArea = {})
    {
        image = frame;
        result = tracking;
        inferenceRegion = trackedArea;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#12151b"));
        if (image.isNull()) {
            painter.setPen(QColor("#e1e1e4"));
            painter.drawText(rect(), Qt::AlignCenter, "Choose a video input to begin");
            return;
        }
        const auto area = imageRect();
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.save();
        if (mirror) {
            painter.translate(2 * area.center().x(), 0);
            painter.scale(-1, 1);
        }
        painter.drawImage(area, image);
        painter.restore();

        if (showLandmarks && !inferenceRegion.isEmpty())
            drawTrackingOverlay(painter, toPreview(inferenceRegion, area), result, mirror);
        if (editRoi || roi != QRectF(0, 0, 1, 1)) {
            const auto selection = toPreview(roi, area);
            QPainterPath shade;
            shade.addRect(area);
            shade.addRect(selection);
            painter.fillPath(shade, QColor(0, 0, 0, 214));
            painter.setPen(QPen(QColor("#a5c5e9"), 1.2, editRoi ? Qt::DashLine : Qt::SolidLine));
            painter.drawRect(selection.adjusted(1, 1, -1, -1));
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor("#a5c5e9"));
            for (const auto &corner : {selection.topLeft(), selection.topRight(), selection.bottomLeft(), selection.bottomRight()})
                painter.drawEllipse(corner, 2.5, 2.5);
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
    QRectF toPreview(const QRectF &region, const QRectF &area) const
    {
        const auto x = mirror ? 1 - region.right() : region.x();
        return QRectF(area.x() + x * area.width(), area.y() + region.y() * area.height(),
                      region.width() * area.width(), region.height() * area.height());
    }

    QRectF imageRect() const
    {
        if (image.isNull()) return {};
        const QSize size = image.size().scaled(this->size(), Qt::KeepAspectRatio);
        return QRectF(QPointF((width() - size.width()) / 2., (height() - size.height()) / 2.), size);
    }
    QPointF imagePoint(const QPointF &point) const
    {
        const auto area = imageRect();
        const auto x = std::clamp((point.x() - area.x()) / std::max(1., area.width()), 0., 1.);
        return {mirror ? 1 - x : x,
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
    QJsonObject result;
    QRectF inferenceRegion;
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

enum class VideoSource { None, Camera, File, Shared };

struct DetectionOptions {
    int confidence = 50, tracking = 50, hands = 2;

    QByteArray packet() const
    {
        return QByteArray::number(confidence) + ' ' + QByteArray::number(tracking) + ' ' + QByteArray::number(hands);
    }
};

class Window : public QWidget {
public:
    Window()
    {
        setWindowTitle(QStringLiteral(MPOSC_APP_NAME));
        const QSettings settings;
        resize(1120, 690);
        restoreGeometry(settings.value("window/geometry").toByteArray());
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 12, 12, 8);
        layout->setSpacing(10);
        installMenus(layout);
        const auto section = [layout](const QString &name) {
            auto *box = new QGroupBox(name);
            box->setObjectName("section");
            auto *row = new QHBoxLayout(box);
            row->setContentsMargins(12, 13, 12, 9);
            row->setSpacing(8);
            layout->addWidget(box);
            return row;
        };
        auto *source = section("Video Input");
        inputModes = new QButtonGroup(this);
        inputPages = new QStackedWidget;
        auto *cameraMode = new QPushButton("Camera");
        auto *fileMode = new QPushButton("Video File");
        auto *cameraPage = new QWidget;
        auto *cameraLayout = new QHBoxLayout(cameraPage);
        cameraLayout->setContentsMargins(0, 0, 0, 0);
        auto *filePage = new QWidget;
        auto *fileLayout = new QHBoxLayout(filePage);
        fileLayout->setContentsMargins(0, 0, 0, 0);
        cameraChoice = new QComboBox;
        for (const auto &device : QMediaDevices::videoInputs()) cameraChoice->addItem(device.description(), device.id());
        const int previousCamera = cameraChoice->findData(settings.value("video/cameraId").toByteArray());
        if (previousCamera >= 0) cameraChoice->setCurrentIndex(previousCamera);
        auto *startCamera = new QPushButton("Start camera");
        startCamera->setEnabled(cameraChoice->count() != 0);
        auto *open = new QPushButton("Browse…");
        auto *fileName = new QLabel("Choose a movie file");
        fileName->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        fileLayout->addWidget(fileName, 1);
        fileLayout->addWidget(open);
        cameraLayout->addWidget(cameraChoice, 1);
        cameraLayout->addWidget(startCamera);
        auto *stop = new QPushButton("Stop");
        faceTracking = new QPushButton("Face");
        handTracking = new QPushButton("Hands");
        bodyTracking = new QPushButton("Body");
        const auto styleToggle = [](QPushButton *button) {
            button->setCheckable(true);
            button->setObjectName("toggle");
        };
        for (auto *button : {faceTracking, handTracking, bodyTracking}) styleToggle(button);
        const int previousTracking = settings.value("video/trackingMask", 4).toInt();
        faceTracking->setChecked(previousTracking & 4);
        handTracking->setChecked(previousTracking & 2);
        bodyTracking->setChecked(previousTracking & 8);
        const auto addInputMode = [&](QPushButton *mode, QWidget *page, const char *key) {
            styleToggle(mode);
            mode->setProperty("sourceKey", key);
            inputModes->addButton(mode, inputPages->addWidget(page));
            source->addWidget(mode);
        };
        addInputMode(cameraMode, cameraPage, "camera");
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
#ifdef Q_OS_MACOS
        const auto sharedLabel = QStringLiteral("Syphon");
        sharedFlip = settings.value("video/sharedFlip", true).toBool();
#else
        const auto sharedLabel = QStringLiteral("Spout");
        sharedFlip = settings.value("video/sharedFlip", false).toBool();
#endif
        auto *sharedMode = new QPushButton(sharedLabel);
        auto *sharedPage = new QWidget;
        auto *sharedLayout = new QHBoxLayout(sharedPage);
        sharedLayout->setContentsMargins(0, 0, 0, 0);
        auto *sharedName = new QLabel("Select a running sender");
        sharedName->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto *sharedButton = new QPushButton("Choose sender…");
        sharedLayout->addWidget(sharedName, 1);
        sharedLayout->addWidget(sharedButton);
        addInputMode(sharedMode, sharedPage, "shared");
        connect(sharedButton, &QPushButton::clicked, this, [this, sharedButton, sharedLabel, sharedName] {
            QMenu menu;
            for (const auto &sender : sharedSenders()) {
                auto *action = menu.addAction(sender.label);
                action->setData(sender.id);
            }
            if (menu.isEmpty()) menu.addAction("No senders found")->setEnabled(false);
            menu.addSeparator();
            auto *flip = menu.addAction("Flip vertically");
            flip->setCheckable(true);
            flip->setChecked(sharedFlip);
            const auto *choice = menu.exec(sharedButton->mapToGlobal(QPoint(0, sharedButton->height())));
            if (!choice) return;
            if (choice == flip) {
                sharedFlip = flip->isChecked();
                if (videoSource == VideoSource::Shared) {
                    ++sourceGeneration;
                    latest = {};
                }
                return;
            }
            auto receiver = openSharedSender(choice->data().toString());
            if (!receiver) {
                status->setText(QStringLiteral("Could not open %1 sender").arg(sharedLabel));
                return;
            }
            stopSource();
            sharedInput = std::move(receiver);
            videoSource = VideoSource::Shared;
            stopButton->setEnabled(true);
            sharedName->setText(choice->text());
            status->setText(QStringLiteral("Waiting for %1 · %2").arg(sharedLabel, choice->text()));
        });
#endif
        addInputMode(fileMode, filePage, "file");
        selectInputMode(settings.value("video/inputMode", "camera").toString());
        connect(inputModes, &QButtonGroup::idClicked, inputPages, &QStackedWidget::setCurrentIndex);
        source->addSpacing(8);
        source->addWidget(inputPages, 1);
        source->addWidget(stop);
        stopButton = stop;
        stopButton->setEnabled(false);

        auto *detection = section("Detection");
        for (auto *button : {faceTracking, handTracking, bodyTracking}) detection->addWidget(button);
        detection->addSpacing(8);
        const auto field = [&](const QString &label, const QString &tip, int max, int min,
                               int DetectionOptions::*member, int scale) {
            auto *slider = new SliderField;
            slider->setRange(double(min) / scale, double(max) / scale);
            slider->setDecimals(scale == 1 ? 0 : 2);
            slider->setSingleStep(scale == 1 ? 1 : .01);
            slider->setMinimumWidth(78);
            slider->setMaximumWidth(130);
            slider->setAccessibleName(label);
            slider->setToolTip(tip);
            detection->addWidget(new QLabel(label));
            detection->addWidget(slider, 1);
            connect(slider, &SliderField::editingFinished, this, [this, member, scale](double value) {
                detectionOptions.*member = qRound(value * scale);
            });
            return slider;
        };
        minDetection = field("Min detection", "Confidence needed to acquire a new subject.", 100, 0,
                             &DetectionOptions::confidence, 100);
        minTracking = field("Min tracking", "Confidence needed to keep tracking between frames.", 100, 0,
                            &DetectionOptions::tracking, 100);
        maxHands = field("Max hands", "Maximum simultaneous hand detections.", 2, 1,
                         &DetectionOptions::hands, 1);
        maxHands->setEnabled(handTracking->isChecked());
        connect(handTracking, &QPushButton::toggled, maxHands, &QWidget::setEnabled);
        loadDetectionOptions(settings);

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
        const auto savedRoi = settings.value("video/roi", fullFrame).toRectF().intersected(fullFrame);
        if (!savedRoi.isEmpty()) picture->roi = savedRoi;
        auto *previewPanel = new QWidget;
        auto *previewLayout = new QVBoxLayout(previewPanel);
        previewLayout->setContentsMargins(0, 0, 0, 0);
        previewLayout->setSpacing(7);
        previewLayout->addWidget(picture, 1);
        auto *previewTools = new QHBoxLayout;
        auto *roi = new QPushButton("Draw ROI");
        auto *resetRoi = new QPushButton("Reset ROI");
        auto *mirror = new QPushButton("Mirror");
        auto *overlay = new QPushButton("Landmarks");
        for (auto *button : {roi, mirror, overlay}) styleToggle(button);
        mirror->setToolTip("Mirror the preview only. OSC coordinates stay unchanged.");
        overlay->setToolTip("Show landmarks and skeletons on the preview.");
        mirror->setChecked(settings.value("window/mirror", false).toBool());
        overlay->setChecked(settings.value("window/landmarks", true).toBool());
        picture->mirror = mirror->isChecked();
        picture->showLandmarks = overlay->isChecked();
        previewMirror = mirror;
        previewLandmarks = overlay;
        connect(mirror, &QPushButton::toggled, picture, [this](bool on) { picture->mirror = on; picture->update(); });
        connect(overlay, &QPushButton::toggled, picture, [this](bool on) { picture->showLandmarks = on; picture->update(); });
        previewTools->addWidget(roi);
        previewTools->addWidget(resetRoi);
        previewTools->addStretch();
        previewTools->addWidget(mirror);
        previewTools->addWidget(overlay);
        previewLayout->addLayout(previewTools);
        resolution = new QLabel("No video frame");
        telemetry = new QLabel("Waiting for input");
        previewLayout->addWidget(resolution);
        previewLayout->addWidget(telemetry);
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
        split->addWidget(previewPanel);
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

        connect(&cameraSink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
            if (videoSource == VideoSource::Camera && frame.isValid()) receiveFrame(frame.toImage());
        });
        connect(&fileSink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
            if (videoSource == VideoSource::File && frame.isValid()) receiveFrame(frame.toImage());
        });
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
                    videoSource = VideoSource::Camera;
                    stopButton->setEnabled(true);
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
        connect(open, &QPushButton::clicked, this, [this, fileName] {
            QSettings settings;
            const auto path = QFileDialog::getOpenFileName(this, "Open video", settings.value("video/lastDirectory").toString());
            if (path.isEmpty()) return;
            settings.setValue("video/lastDirectory", QFileInfo(path).absolutePath());
            fileName->setText(QFileInfo(path).fileName());
            stopSource();
            videoSource = VideoSource::File;
            stopButton->setEnabled(true);
            player.setSource(QUrl::fromLocalFile(path));
            player.play();
        });
        connect(stop, &QPushButton::clicked, this, [this] { stopSource(); });
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
                const bool current = submittedMask == trackingMask() && submittedOptions == detectionOptions.packet()
                    && submittedGeneration == sourceGeneration && submittedSelection == picture->roi && !picture->isDrawing();
                const QRectF region(double(submittedRoi.x()) / submittedSource.width(),
                                    double(submittedRoi.y()) / submittedSource.height(),
                                    double(submittedRoi.width()) / submittedSource.width(),
                                    double(submittedRoi.height()) / submittedSource.height());
                picture->showFrame(submittedSource, current ? result : QJsonObject(), region);
                if (current) {
                    updateTelemetry(result);
                    updateSignals(result);
                }
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
            if (busy) return;
            if (sharedInput) {
                auto frame = sharedInput->frame();
                if (sharedFlip && !frame.isNull()) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
                    frame.flip(Qt::Vertical);
#else
                    frame = frame.mirrored(false, true);
#endif
                }
                receiveFrame(std::move(frame));
            }
            if (latest.isNull()) return;
            if (picture->isDrawing()) {
                picture->showFrame(latest);
                return;
            }
            if (!ready) return;
            submittedSource = std::exchange(latest, QImage()).convertToFormat(QImage::Format_RGBA8888);
            submittedSelection = picture->roi;
            const auto &roi = submittedSelection;
            const int width = submittedSource.width(), height = submittedSource.height();
            submittedRoi = QRectF(roi.x() * width, roi.y() * height, roi.width() * width, roi.height() * height)
                               .toAlignedRect().intersected(submittedSource.rect());
            submitted = submittedRoi == submittedSource.rect() ? submittedSource : submittedSource.copy(submittedRoi);
            if (previousRoi != submittedRoi) {
                previousRoi = submittedRoi;
                ++sourceGeneration;
            }
            submittedMask = trackingMask();
            submittedOptions = detectionOptions.packet();
            submittedGeneration = sourceGeneration;
            QByteArray packet = QByteArray::number(submitted.width()) + ' ' + QByteArray::number(submitted.height()) +
                                ' ' + QByteArray::number(submittedMask) + ' ' + submittedOptions +
                                ' ' + QByteArray::number(submittedGeneration) + '\n';
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
        settings.setValue("video/sharedFlip", sharedFlip);
        if (const auto *mode = inputModes->checkedButton()) settings.setValue("video/inputMode", mode->property("sourceKey"));
        settings.setValue("video/minDetection", detectionOptions.confidence);
        settings.setValue("video/minTracking", detectionOptions.tracking);
        settings.setValue("video/maxHands", detectionOptions.hands);
        settings.setValue("window/mirror", previewMirror->isChecked());
        settings.setValue("window/landmarks", previewLandmarks->isChecked());
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
    void selectInputMode(const QString &key)
    {
        int id = 0;
        for (auto *button : inputModes->buttons()) {
            if (button->property("sourceKey").toString() != key) continue;
            id = inputModes->id(button);
            break;
        }
        if (auto *button = inputModes->button(id)) button->setChecked(true);
        inputPages->setCurrentIndex(id);
    }

    static bool configurationKey(const QString &key)
    {
        return key == "destinations" || key.startsWith("signals/") || key.startsWith("video/") ||
               key.startsWith("window/") || key.startsWith("osc/");
    }

    void installMenus(QVBoxLayout *layout)
    {
        auto *bar = new QMenuBar(this);
        layout->setMenuBar(bar);
        auto *file = bar->addMenu("&File");
        auto *open = file->addAction("&Open…");
        open->setShortcut(QKeySequence::Open);
        connect(open, &QAction::triggered, this, [this] { loadConfiguration(); });
        auto *recent = file->addMenu("Open &Recent");
        connect(recent, &QMenu::aboutToShow, this, [this, recent] {
            recent->clear();
            const auto paths = QSettings().value("recentConfigurations").toStringList();
            for (const auto &path : paths) {
                auto *action = recent->addAction(QFileInfo(path).fileName());
                action->setToolTip(path);
                action->setEnabled(QFileInfo::exists(path));
                connect(action, &QAction::triggered, this, [this, path] { loadConfiguration(path); });
            }
            if (paths.isEmpty()) recent->addAction("No recent configurations")->setEnabled(false);
        });
        file->addSeparator();
        auto *save = file->addAction("&Save");
        save->setShortcut(QKeySequence::Save);
        connect(save, &QAction::triggered, this, [this] { saveConfiguration(false); });
        auto *saveAs = file->addAction("Save &As…");
        saveAs->setShortcut(QKeySequence::SaveAs);
        connect(saveAs, &QAction::triggered, this, [this] { saveConfiguration(true); });

        auto *about = bar->addMenu("&Help")->addAction("About MpOsc");
        about->setMenuRole(QAction::AboutRole);
        connect(about, &QAction::triggered, this, [this] {
            QMessageBox dialog(this);
            dialog.setWindowTitle("About MpOsc");
            dialog.setIconPixmap(QApplication::windowIcon().pixmap(96, 96));
            dialog.setText(QStringLiteral("<h2>MpOsc</h2><p>Motion Processing OSC · %1</p>")
                               .arg(QApplication::applicationVersion()));
            dialog.setInformativeText("Turn movement into live OSC controls.\nBuilt by Pixel Stereo.");
            dialog.setStandardButtons(QMessageBox::Ok);
            dialog.exec();
        });
    }

    void rememberConfiguration(const QString &path)
    {
        configurationPath = QFileInfo(path).absoluteFilePath();
        setWindowTitle(QStringLiteral(MPOSC_APP_NAME) + " — " + QFileInfo(path).completeBaseName());
        QSettings settings;
        auto recent = settings.value("recentConfigurations").toStringList();
        recent.removeAll(configurationPath);
        recent.prepend(configurationPath);
        while (recent.size() > 8) recent.removeLast();
        settings.setValue("recentConfigurations", recent);
    }

    void loadDetectionOptions(const QSettings &settings)
    {
        detectionOptions.confidence = std::clamp(settings.value("video/minDetection", 50).toInt(), 0, 100);
        detectionOptions.tracking = std::clamp(settings.value("video/minTracking", 50).toInt(), 0, 100);
        detectionOptions.hands = std::clamp(settings.value("video/maxHands", 2).toInt(), 1, 2);
        minDetection->setValue(detectionOptions.confidence / 100.0);
        minTracking->setValue(detectionOptions.tracking / 100.0);
        maxHands->setValue(detectionOptions.hands);
    }

    void updateTelemetry(const QJsonObject &result)
    {
        if (!inferenceClock.isValid()) inferenceClock.start();
        ++inferenceFrames;
        if (inferenceClock.elapsed() >= 750) {
            inferenceRate = inferenceFrames * 1000.0 / inferenceClock.restart();
            inferenceFrames = 0;
        }
        const auto rate = inferenceRate > 0 ? QString::number(inferenceRate, 'f', 1) + " fps" : QStringLiteral("— fps");
        const auto ms = result.value("inference_ms");
        const auto duration = ms.isDouble() ? QString::number(ms.toDouble(), 'f', 1) + " ms" : QStringLiteral("— ms");
        QStringList labels{rate, duration};
        for (const auto &entry : {std::pair{"face", "Face"}, {"hands", "Hands"}, {"pose", "Body"}})
            if (result.contains(entry.first)) labels << QStringLiteral("%1 %2").arg(entry.second).arg(result.value(entry.first).toArray().size());
        telemetry->setText(labels.join(QStringLiteral("  ·  ")));
    }

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

    void saveConfiguration(bool saveAs)
    {
        auto path = configurationPath;
        if (saveAs || path.isEmpty()) path = QFileDialog::getSaveFileName(this, "Save MpOsc configuration", path, "MpOsc (*.mposc)");
        if (path.isEmpty()) return;
        if (!path.endsWith(".mposc", Qt::CaseInsensitive)) path += ".mposc";
        QTemporaryDir staging;
        if (!staging.isValid()) {
            QMessageBox::warning(this, "MpOsc", "Could not create a temporary configuration file.");
            return;
        }
        QSettings local;
        storeSettings(local);
        QSettings snapshot(staging.filePath("snapshot.ini"), QSettings::IniFormat);
        for (const auto &key : local.allKeys())
            if (configurationKey(key)) snapshot.setValue(key, local.value(key));
        snapshot.setValue("formatVersion", 1);
        snapshot.sync();
        QFile data(snapshot.fileName());
        QSaveFile file(path);
        if (snapshot.status() != QSettings::NoError || !data.open(QIODevice::ReadOnly) || !file.open(QIODevice::WriteOnly)) {
            QMessageBox::warning(this, "MpOsc", "Could not save the configuration.");
            return;
        }
        const auto bytes = data.readAll();
        if (data.error() != QFileDevice::NoError || file.write(bytes) != bytes.size() || !file.commit()) {
            QMessageBox::warning(this, "MpOsc", "Could not save the configuration.");
            return;
        }
        rememberConfiguration(path);
        status->setText("Configuration saved");
    }

    void loadConfiguration(QString path = {})
    {
        if (path.isEmpty()) path = QFileDialog::getOpenFileName(this, "Open MpOsc configuration", configurationPath, "MpOsc (*.mposc)");
        if (path.isEmpty()) return;
        const QSettings snapshot(path, QSettings::IniFormat);
        if (snapshot.value("formatVersion").toInt() != 1 || snapshot.status() != QSettings::NoError) {
            QMessageBox::warning(this, "MpOsc", "This file is not a supported MpOsc configuration.");
            return;
        }
        stopSource();
        QSettings local;
        for (const auto &group : {"signals", "video", "window", "osc", "destinations"}) local.remove(group);
        for (const auto &key : snapshot.allKeys()) {
            if (configurationKey(key)) local.setValue(key, snapshot.value(key));
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
#ifdef Q_OS_MACOS
        sharedFlip = local.value("video/sharedFlip", true).toBool();
#else
        sharedFlip = local.value("video/sharedFlip", false).toBool();
#endif
        selectInputMode(local.value("video/inputMode", "camera").toString());
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
        previewMirror->setChecked(local.value("window/mirror", false).toBool());
        previewLandmarks->setChecked(local.value("window/landmarks", true).toBool());
        loadDetectionOptions(local);
        restoreSignalRows(local);
        rememberConfiguration(path);
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

    void receiveFrame(QImage frame)
    {
        if (frame.isNull()) return;
        latest = std::move(frame);
        if (latest.size() != sourceSize) {
            sourceSize = latest.size();
            resolution->setText(QStringLiteral("%1 × %2 px (%3 pixels)")
                .arg(sourceSize.width()).arg(sourceSize.height())
                .arg(locale().toString(qint64(sourceSize.width()) * sourceSize.height())));
        }
        if (!ready) showFrame(latest);
    }

    void stopSource()
    {
        videoSource = VideoSource::None;
        stopButton->setEnabled(false);
        sharedInput.reset();
        if (camera) camera->stop();
        player.stop();
        latest = {};
        active = false;
        sourceSize = {};
        resolution->setText("No video frame");
        telemetry->setText("Waiting for input");
        inferenceClock.invalidate();
        inferenceRate = 0;
        inferenceFrames = 0;
        ++sourceGeneration;
        picture->showFrame({});
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
        signal->setObjectName("signalName");
        auto *nameCell = new QFrame;
        nameCell->setObjectName("signalCell");
        const auto updateOutline = [nameCell](bool enabled) {
            nameCell->setProperty("oscEnabled", enabled);
            nameCell->style()->unpolish(nameCell);
            nameCell->style()->polish(nameCell);
            nameCell->update();
        };
        updateOutline(signal->isChecked());
        connect(signal, &QPushButton::toggled, nameCell, updateOutline);
        auto *nameLayout = new QHBoxLayout(nameCell);
        nameLayout->setContentsMargins(1, 1, 1, 1);
        nameLayout->setSpacing(4);
        auto *pin = new PinButton;
        pin->setChecked(pinned);
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
        meter->setObjectName("signalValue");
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
    QLabel *resolution, *telemetry;
    QSize sourceSize;
    QByteArray workerLog;
    bool workerRetried = false, shuttingDown = false;
    QLineEdit *destinations, *search;
    QPushButton *send, *previewMirror, *previewLandmarks, *stopButton;
    QButtonGroup *inputModes;
    QStackedWidget *inputPages;
    SliderField *minDetection, *minTracking, *maxHands;
    DetectionOptions detectionOptions;
    QString configurationPath;
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
    std::unique_ptr<SharedInput> sharedInput;
    VideoSource videoSource = VideoSource::None;
    QProcess worker;
    QUdpSocket udp;
    QTimer timer;
    QElapsedTimer clock, inferenceClock;
    double inferenceRate = 0;
    int inferenceFrames = 0;
    QImage latest, submitted, submittedSource;
    QRect submittedRoi, previousRoi;
    QByteArray submittedOptions;
    QRectF submittedSelection;
    int sourceGeneration = 0, submittedGeneration = 0;
    int submittedMask = 0;
    bool ready = false, busy = false, active = false, sharedFlip = false;
};
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("MpOsc");
    app.setApplicationVersion(QStringLiteral(MPOSC_VERSION));
    app.setWindowIcon(QIcon(":/assets/MpOsc.png"));
    app.setOrganizationName("PixelStereo");
    app.setStyle("Fusion");
    auto *hints = QGuiApplication::styleHints();
    applyInterfacePalette(app, hints->colorScheme());
    QObject::connect(hints, &QStyleHints::colorSchemeChanged, &app, [&app](Qt::ColorScheme scheme) {
        applyInterfacePalette(app, scheme);
    });
    if (app.arguments().contains("--check-mediapipe")) {
        const auto root = runtimeFolder();
        QProcess worker;
        worker.start(pythonExecutable(root), {"-I", "-u", "-c", QString::fromUtf8(workerScript), root});
        if (!worker.waitForStarted()) return 1;
        const QByteArray pixels(128 * 128 * 4, '\0');
        worker.write(QByteArray("128 128 4110 40 60 1 0\n") + pixels);
        worker.write(QByteArray("128 128 14 40 60 1 0\n") + pixels);
        worker.write(QByteArray("128 128 14 50 50 2 1\n") + pixels);
        worker.closeWriteChannel();
        if (!worker.waitForFinished(60000)) return 2;
        const auto lines = worker.readAllStandardOutput().trimmed().split('\n');
        if (lines.size() != 4 || !QJsonDocument::fromJson(lines[0]).object().value("ready").toBool()) return 3;
        const auto frame = QJsonDocument::fromJson(lines[1]).object();
        if (!frame.contains("face") || !frame.contains("hands") || !frame.contains("pose") || !frame.contains("preview_jpeg")) {
            qCritical().noquote() << lines[1];
            return 4;
        }
        for (int i = 2; i < 4; ++i) {
            const auto sample = QJsonDocument::fromJson(lines[i]).object();
            if (!sample.value("inference_ms").isDouble() || !sample.contains("face") ||
                !sample.contains("hands") || !sample.contains("pose") || sample.contains("preview_jpeg")) {
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
        qInfo("Packaged inference, annotations, head rotation, inversion and two OSC destinations passed");
        return 0;
    }
    QSettings settings;
    if (settings.allKeys().isEmpty()) {
        const QSettings legacy("PixelStereo", "MP_OSC");
        for (const auto &key : legacy.allKeys()) settings.setValue(key, legacy.value(key));
    }
    Window window;
    window.show();
    return app.exec();
}
