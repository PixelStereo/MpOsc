#include "OscCodec.h"
#include "SliderField.h"
#include "Destinations.h"
#include "MediaPipeClient.h"
#include "MediaPipeRuntime.h"
#include "SelfTest.h"
#include "HoverHints.h"
#include "SignalPanel.h"
#include "Signals.h"
#include "About.h"
#include "Theme.h"
#include "VideoInput.h"
#include "VideoOutput.h"
#include <pxs/Bind.h>
#include <pxs/OscService.h>
#include <pxs/Convert.h>
#include <pxs/core/tree/tree.h>
#include "VideoPreview.h"
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
#include <vector>
#include <cmath>

namespace {

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
        cameraQuality = new QComboBox;
        cameraQuality->setToolTip("Resolution requested from the camera. Lower values reduce USB bandwidth and decoding work. "
                                  "Detection runs on a smaller copy anyway (see Analysis size).");
        cameraQuality->setAccessibleName("Camera resolution");
        cameraQuality->addItem("Camera: auto (up to 1080p)", qint64(1920) * 1080);
        cameraQuality->addItem("Camera: maximum", qint64(0));
        cameraQuality->addItem("Camera: 720p", qint64(1280) * 720);
        cameraQuality->addItem("Camera: 480p", qint64(640) * 480);
        auto *startCamera = new QPushButton("Start camera");
        startCameraButton = startCamera;
        startCamera->setEnabled(cameraChoice->count() != 0);
        auto *open = new QPushButton("Browse…");
        auto *fileName = new QLabel("Choose a movie file");
        fileName->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        fileLayout->addWidget(fileName, 1);
        fileLayout->addWidget(open);
        cameraLayout->addWidget(cameraChoice, 1);
        cameraLayout->addWidget(cameraQuality);
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
        const auto addStreamMode = [&](QString label, const char *key, StreamMode &mode) {
            mode.label = label;
            auto *button = new QPushButton(label);
            auto *page = new QWidget;
            auto *row = new QHBoxLayout(page);
            row->setContentsMargins(0, 0, 0, 0);
            mode.name = new QLabel("Select a running sender");
            mode.name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            auto *choose = new QPushButton("Choose sender…");
            row->addWidget(mode.name, 1);
            row->addWidget(choose);
            addInputMode(button, page, key);
            mode.page = inputModes->id(button);
            connect(choose, &QPushButton::clicked, this, [this, choose, &mode] {
                QMenu menu;
                for (const auto &sender : streamSenders(mode)) {
                    auto *action = menu.addAction(sender.label);
                    action->setData(sender.id);
                }
                if (menu.isEmpty()) menu.addAction("No senders found")->setEnabled(false);
                QAction *flip = nullptr;
                if (&mode == &sharedMode) {
                    menu.addSeparator();
                    flip = menu.addAction("Flip vertically");
                    flip->setCheckable(true);
                    flip->setChecked(video.sharedFlip());
                }
                const auto *choice = menu.exec(choose->mapToGlobal(QPoint(0, choose->height())));
                if (!choice) return;
                if (choice == flip) {
                    video.setSharedFlip(flip->isChecked());
                    return;
                }
                auto receiver = openStream(mode, choice->data().toString());
                if (receiver)
                    startStream(mode, choice->text(), std::move(receiver));
                else
                    status->setText(QStringLiteral("Could not open %1 sender").arg(mode.label));
            });
        };
#ifdef Q_OS_MACOS
        video.setSharedFlip(settings.value("video/sharedFlip", true).toBool());
        addStreamMode("Syphon", "shared", sharedMode);
#elif defined(Q_OS_WIN)
        video.setSharedFlip(settings.value("video/sharedFlip", false).toBool());
        addStreamMode("Spout", "shared", sharedMode);
#endif
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
        addStreamMode("OMT", "omt", omtMode);
#endif
        addInputMode(fileMode, filePage, "file");
        connect(inputModes, &QButtonGroup::idClicked, this, [this](int id) {
            if (inputPages->currentIndex() == id &&
                (pendingStream || video.source() != VideoInput::Source::None)) return;
            stopSource();
            inputPages->setCurrentIndex(id);
            activateStreamMode();
        });
        source->addSpacing(8);
        source->addWidget(inputPages, 1);
        source->addWidget(stop);
        stopButton = stop;
        stopButton->setEnabled(false);

        auto *detection = section("Detection");
        // A caption right above its control, left-aligned with it.
        const auto captioned = [](const QString &label, QWidget *control) {
            auto *cell = new QWidget;
            auto *column = new QVBoxLayout(cell);
            column->setContentsMargins(0, 0, 0, 0);
            column->setSpacing(3);
            auto *caption = new QLabel(label);
            caption->setObjectName("caption");
            column->addWidget(caption);
            column->addWidget(control);
            return cell;
        };
        analysisSize = new QComboBox;
        analysisSize->setAccessibleName("Analysis size");
        analysisSize->setToolTip("Longest side of the image sent to MediaPipe. The models resize their input to a few hundred "
                                 "pixels internally, so a larger image mostly costs time. The preview always stays full size, "
                                 "and the ROI is cropped from the full-resolution frame before resizing.");
        analysisSize->addItem("Original (no resize)", 0);
        analysisSize->addItem("1280 px", 1280);
        analysisSize->addItem("960 px", 960);
        analysisSize->addItem("640 px (recommended)", 640);
        analysisSize->addItem("480 px", 480);
        analysisSize->addItem("320 px", 320);
        // One row: the analysis size, the Face / Hands / Body buttons beside it, then the three settings
        detection->addWidget(captioned("Analysis size", analysisSize), 0, Qt::AlignTop);
        detection->addSpacing(8);
        auto *trackingButtons = new QHBoxLayout;
        for (auto *button : {faceTracking, handTracking, bodyTracking}) {
            button->setMinimumSize(78, 36); // large enough to hit quickly during a live show
            auto font = button->font();
            font.setPointSizeF(font.pointSizeF() * 1.1);
            button->setFont(font);
            trackingButtons->addWidget(button);
        }
        auto *buttonColumn = new QVBoxLayout; // bottom-aligned with the combo, whose caption sits above it
        buttonColumn->addStretch(1);
        buttonColumn->addLayout(trackingButtons);
        detection->addLayout(buttonColumn);
        detection->addSpacing(16);
        const auto field = [&](const QString &label, const QString &tip, int max, int min,
                               int DetectionOptions::*member, int scale) {
            auto *slider = new SliderField;
            slider->setRange(double(min) / scale, double(max) / scale);
            slider->setDecimals(scale == 1 ? 0 : 2);
            slider->setSingleStep(scale == 1 ? 1 : .01);
            slider->setMinimumWidth(170);
            slider->setMaximumWidth(220);
            slider->setAccessibleName(label);
            slider->setToolTip(tip);
            detection->addWidget(captioned(label, slider), 0, Qt::AlignTop);
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
        detection->addStretch(1);
        maxHands->setEnabled(handTracking->isChecked());
        connect(handTracking, &QPushButton::toggled, maxHands, &QWidget::setEnabled);
        loadDetectionOptions(settings);
        connect(analysisSize, &QComboBox::currentIndexChanged, this, [this] {
            analysisEdge = analysisSize->currentData().toInt();
            ++sourceGeneration;
            updateSizeLabel();
        });

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
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
        auto *videoOutput = section("Video Output");
        auto *composition = new QComboBox;
        composition->addItem("Image + landmarks");
        composition->addItem("Landmarks only");
        videoOutput->addWidget(composition);
        connect(composition, &QComboBox::currentIndexChanged, this, [this](int index) {
            landmarksOnly = index == 1;
        });
        const auto addOutput = [&](const QString &label, std::unique_ptr<VideoOutput> &output,
                                   std::unique_ptr<VideoOutput> (*openOutput)(const QString &)) {
            auto *button = new QPushButton(label);
            styleToggle(button);
            videoOutput->addWidget(button);
            connect(button, &QPushButton::toggled, this, [this, button, label, &output, openOutput](bool on) {
                if (!on) {
                    output.reset();
                    return;
                }
                output = openOutput(QStringLiteral("MpOsc Landmarks"));
                if (output) return;
                const QSignalBlocker block(button);
                button->setChecked(false);
                status->setText(QStringLiteral("Could not start %1 video output").arg(label));
            });
        };
#ifdef Q_OS_MACOS
        addOutput("Syphon", syphonOutput, openSyphonOutput);
#endif
        addOutput("OMT", omtOutput, openOmtOutput);
        videoOutput->addStretch();
#endif

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
        resolution = new QLabel("No video frame");
        telemetry = new QLabel(analysisStatus);
        previewLayout->addWidget(resolution);
        previewLayout->addLayout(previewTools);
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
        signalPanel = new SignalPanel;
        split->addWidget(signalPanel);
        filtersAction->setChecked(settings.value("window/filters", false).toBool());
        signalPanel->setFiltersShown(filtersAction->isChecked());
        connect(filtersAction, &QAction::toggled, this, [this](bool shown) { signalPanel->setFiltersShown(shown); });
        if (!split->restoreState(settings.value("window/split").toByteArray()))
            split->setSizes({520, 600});
        layout->addWidget(split, 1);
        status = new QLabel("Ready");
        hint = new QLabel;
        hint->setObjectName("hint");
        hint->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        hint->setForegroundRole(QPalette::PlaceholderText);
        hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto *statusBar = new QHBoxLayout;
        statusBar->addWidget(status, 1);
        statusBar->addWidget(hint, 2); // what the control under the pointer is for
        layout->addLayout(statusBar);
        HoverHints::install(hint);
        connect(&video, &VideoInput::frameReady, this, [this](const QImage &frame) { receiveFrame(frame); });
        connect(&video, &VideoInput::errorOccurred, this, [this](const QString &error) { status->setText(error); });
        connect(&video, &VideoInput::sourceReset, this, [this] {
            ++sourceGeneration;
            latest = {};
        });
        auto openCamera = [this] {
            stopSource();
            if (video.startCamera(cameraChoice->currentData().toByteArray(), cameraQuality->currentData().toLongLong()))
                stopButton->setEnabled(true);
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
            video.openFile(path);
            stopButton->setEnabled(true);
        });
        connect(cameraQuality, &QComboBox::currentIndexChanged, this, [this, startCamera] {
            QSettings().setValue("video/cameraQuality", cameraQuality->currentData());
            if (video.source() == VideoInput::Source::Camera) startCamera->click();
        });
        connect(stop, &QPushButton::clicked, this, [this] { stopSource(); });
        connect(destinations, &QLineEdit::editingFinished, this, [this] {
            QSettings().setValue("destinations", destinations->text());
            signalPanel->resetSent();
        });
        connect(send, &QPushButton::toggled, this, [this](bool on) { signalPanel->setSending(on); });
        signalPanel->setSending(send->isChecked());
        connect(&mediapipe, &MediaPipeClient::statusChanged, this, [this](const QString &text, const QString &details) {
            analysisStatus = text;
            telemetry->setText(text);
            telemetry->setToolTip(details);
        });
        connect(&mediapipe, &MediaPipeClient::resultReady, this, [this](const QJsonObject &result) {
            if (!active) return;
            const bool current = submittedMask == trackingMask() && submittedOptions == detectionOptions.packet()
                && submittedGeneration == sourceGeneration && submittedSelection == picture->roi && !picture->isDrawing();
            const QRectF region(double(submittedRoi.x()) / submittedSource.width(),
                                double(submittedRoi.y()) / submittedSource.height(),
                                double(submittedRoi.width()) / submittedSource.width(),
                                double(submittedRoi.height()) / submittedSource.height());
            picture->showFrame(submittedSource, current ? result : QJsonObject(), region);
            if (current) {
                updateTelemetry(result);
                status->setText(signalPanel->update(result, destinations->text(), send->isChecked()));
                publishFrame(submittedSource, submittedRoi, result);
            }
        });
        mediapipe.start();
        connect(&timer, &QTimer::timeout, this, [this] {
            pollStreams();
            video.poll();
            if (!sourceSize.isEmpty() && frameRateClock.elapsed() >= 1000) {
                frameRate = frameCount * 1000.0 / frameRateClock.restart();
                frameCount = 0;
                updateSizeLabel();
            }
            if (mediapipe.isBusy()) return;
            if (latest.isNull()) return;
            if (picture->isDrawing()) {
                picture->showFrame(latest);
                return;
            }
            if (!mediapipe.isReady()) return;
            submittedSource = std::exchange(latest, QImage()); // shown as is: no full-size format conversion
            submittedSelection = picture->roi;
            const auto &roi = submittedSelection;
            const int width = submittedSource.width(), height = submittedSource.height();
            submittedRoi = QRectF(roi.x() * width, roi.y() * height, roi.width() * width, roi.height() * height)
                               .toAlignedRect().intersected(submittedSource.rect());
            // Crop at full resolution, then shrink to the analysis size (never enlarged), then convert only that copy.
            submitted = submittedRoi == submittedSource.rect() ? submittedSource : submittedSource.copy(submittedRoi);
            if (analysisEdge > 0 && std::max(submitted.width(), submitted.height()) > analysisEdge)
                submitted = submitted.scaled(analysisEdge, analysisEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            submitted = submitted.convertToFormat(QImage::Format_RGBA8888);
            if (analysedSize != submitted.size()) {
                analysedSize = submitted.size();
                updateSizeLabel();
            }
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
            packet.reserve(packet.size() + int(submitted.sizeInBytes()));
            packet.append(reinterpret_cast<const char *>(submitted.constBits()), submitted.sizeInBytes());
            active = true;
            mediapipe.send(packet);
        });
        signalPanel->setFilter(settings.value("window/search").toString());
        // The signals are listed in the namespace (what MpOsc sends, to the destinations it has itself) before the rows
        // saved in the settings are made
        signalPanel->setSignalKnown([this](const QString &address) {
            const std::string path = pxs::convert::toUtf8(address);
            if (!tree.find(path)) tree.addSignal(path, pxs::convert::toUtf8(signalTypeTag(signalType(address))), "A tracking signal MpOsc sends to its destinations");
        });
        signalPanel->setSignalFired([this](const QString &address, const QVariant &value) { tree.fire(pxs::convert::toUtf8(address), {pxs::convert::toValue(value)}); });
        signalPanel->restoreRows(settings);
        exposeControls(settings);
        selectInputMode(settings.value("video/inputMode", "camera").toString());
        timer.start(8);
    }

    void storeSettings(QSettings &settings)
    {
        settings.setValue("window/search", signalPanel->filter());
        settings.setValue("window/filters", filtersAction->isChecked());
        settings.setValue("window/geometry", saveGeometry());
        settings.setValue("window/split", split->saveState());
        if (cameraChoice->currentIndex() >= 0)
            settings.setValue("video/cameraId", cameraChoice->currentData());
        settings.setValue("video/trackingMask", trackingMask());
        settings.setValue("video/sharedFlip", video.sharedFlip());
        if (const auto *mode = inputModes->checkedButton()) settings.setValue("video/inputMode", mode->property("sourceKey"));
        settings.setValue("video/minDetection", detectionOptions.confidence);
        settings.setValue("video/minTracking", detectionOptions.tracking);
        settings.setValue("video/maxHands", detectionOptions.hands);
        settings.setValue("video/analysisEdge", analysisEdge);
        settings.setValue("video/cameraQuality", cameraQuality->currentData());
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
        timer.stop();
        stopSource();
        mediapipe.shutdown();
        service.reset();
        // Bound widgets unregister their observers when destroyed, while the tree is still alive.
        qDeleteAll(findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly));
    }

private:
    // What the interface has is reachable like every other Pixel Stereo application: OSC and OSCQuery, each parameter
    // going through its own widget, so that writing it from outside does what a click does
    void exposeControls(const QSettings &settings)
    {
        pxs::bindToggle(tree, "/osc/send", send, "Send the enabled signals to the destinations");
        pxs::bindText(tree, "/osc/destinations", destinations, "host:port entries separated by spaces");
        pxs::bindNumber(tree, "/detection/confidence", minDetection, "Confidence needed to acquire a new subject");
        pxs::bindNumber(tree, "/detection/tracking", minTracking, "Confidence needed to keep tracking between frames");
        pxs::bindNumber(tree, "/detection/hands", maxHands, "Maximum simultaneous hand detections");
        pxs::bindChoice(tree, "/analysis/size", analysisSize, "The size of the copy the detection runs on");
        pxs::bindToggle(tree, "/tracking/face", faceTracking, "Track faces");
        pxs::bindToggle(tree, "/tracking/hands", handTracking, "Track hands");
        pxs::bindToggle(tree, "/tracking/body", bodyTracking, "Track the body");
        pxs::bindToggle(tree, "/preview/mirror", previewMirror, "Mirror the preview only; OSC coordinates stay unchanged");
        pxs::bindToggle(tree, "/preview/landmarks", previewLandmarks, "Show landmarks and skeletons on the preview");
        pxs::bindAction(tree, "/video/stop", stopButton, "Stop the video source");
        pxs::bindAction(tree, "/video/camera/start", startCameraButton, "Start the selected camera");
        service = std::make_unique<pxs::OscService>(&tree, QStringLiteral(MPOSC_APP_NAME));
        if (!service->start(quint16(settings.value("control/oscPort", 47821).toUInt()),
                            quint16(settings.value("control/oscQueryPort", 47822).toUInt())) &&
            !service->start(0, 0))
            service.reset();
    }

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
        activateStreamMode();
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
        HoverHints::watch(recent);
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

        auto *view = bar->addMenu("&View");
        filtersAction = view->addAction("Signal &Filters");
        filtersAction->setCheckable(true);
        filtersAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M)); // Command on a Mac
        filtersAction->setToolTip("The filters of the selected signal, with the signal before and after each one");
        HoverHints::watch(view);

        auto *about = bar->addMenu("&Help")->addAction("About MpOsc");
        about->setMenuRole(QAction::AboutRole);
        connect(about, &QAction::triggered, this, [this] {
            showAbout(this);
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
        const QSignalBlocker blockAnalysis(analysisSize), blockCamera(cameraQuality);
        const int edge = analysisSize->findData(settings.value("video/analysisEdge", 640).toInt());
        analysisSize->setCurrentIndex(edge >= 0 ? edge : analysisSize->findData(640));
        analysisEdge = analysisSize->currentData().toInt();
        const int quality = cameraQuality->findData(settings.value("video/cameraQuality", qint64(1920) * 1080).toLongLong());
        cameraQuality->setCurrentIndex(quality >= 0 ? quality : 0);
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
        QStringList labels{QStringLiteral("Analysis %1").arg(rate), duration};
        for (auto entry = result.begin(); entry != result.end(); ++entry) {
            if (entry.key() == "error" || entry.key().endsWith("_error")) {
                telemetry->setText("MediaPipe analysis failed");
                telemetry->setToolTip(entry.value().toString());
                return;
            }
        }
        telemetry->setToolTip({});
        for (const auto &entry : {std::pair{"face", "Face"}, {"hands", "Hands"}, {"pose", "Body"}})
            if (result.contains(entry.first)) labels << QStringLiteral("%1 %2").arg(entry.second).arg(result.value(entry.first).toArray().size());
        telemetry->setText(labels.join(QStringLiteral("  ·  ")));
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
        signalPanel->clear();
        restoreGeometry(local.value("window/geometry").toByteArray());
        split->restoreState(local.value("window/split").toByteArray());
        const int camera = cameraChoice->findData(local.value("video/cameraId").toByteArray());
        cameraChoice->setCurrentIndex(camera >= 0 ? camera : (cameraChoice->count() > 0 ? 0 : -1));
#ifdef Q_OS_MACOS
        video.setSharedFlip(local.value("video/sharedFlip", true).toBool());
#else
        video.setSharedFlip(local.value("video/sharedFlip", false).toBool());
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
        signalPanel->setFilter(local.value("window/search").toString());
        previewMirror->setChecked(local.value("window/mirror", false).toBool());
        previewLandmarks->setChecked(local.value("window/landmarks", true).toBool());
        loadDetectionOptions(local);
        signalPanel->restoreRows(local);
        rememberConfiguration(path);
        status->setText("Configuration loaded");
    }

    struct StreamMode {
        QString label;
        QLabel *name = nullptr;
        int page = -1;
    };

    struct StreamCandidate {
        SharedSender sender;
        std::unique_ptr<SharedInput> receiver;
    };

    QList<SharedSender> streamSenders(const StreamMode &mode)
    {
        if (&mode == &omtMode)
            return withoutOutput(omtSenders(), omtOutput);
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
        return withoutOutput(sharedSenders(), syphonOutput);
#else
        return {};
#endif
    }

    static QList<SharedSender> withoutOutput(QList<SharedSender> senders, const std::unique_ptr<VideoOutput> &output)
    {
        if (output) {
            const auto id = output->senderId();
            senders.removeIf([&](const SharedSender &sender) { return sender.id == id; });
        }
        return senders;
    }

    std::unique_ptr<SharedInput> openStream(const StreamMode &mode, const QString &id)
    {
        if (&mode == &omtMode) return openOmtSender(id);
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
        return openSharedSender(id);
#else
        return {};
#endif
    }

    void activateStreamMode()
    {
        for (auto *mode : {&sharedMode, &omtMode}) {
            if (mode->page != inputPages->currentIndex() || !mode->name) continue;
            if (mode == &omtMode && !omtAvailable()) {
                mode->name->setText("OMT runtime unavailable");
                return;
            }
            pendingStream = mode;
            mode->name->setText(QStringLiteral("Waiting for a %1 sender…").arg(mode->label));
            stopButton->setEnabled(true);
            return;
        }
    }

    void pollStreams()
    {
        if (!pendingStream) return;
        if (!discoveryClock.isValid() || discoveryClock.elapsed() >= 500) {
            discoveryClock.start();
            const auto senders = streamSenders(*pendingStream);
            candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](const StreamCandidate &entry) {
                return std::none_of(senders.begin(), senders.end(), [&](const SharedSender &sender) {
                    return sender.id == entry.sender.id;
                });
            }), candidates.end());
            for (const auto &sender : senders) {
                if (std::any_of(candidates.begin(), candidates.end(), [&](const StreamCandidate &entry) {
                    return entry.sender.id == sender.id;
                })) continue;
                if (auto receiver = openStream(*pendingStream, sender.id))
                    candidates.push_back({sender, std::move(receiver)});
            }
        }
        for (auto &entry : candidates) {
            auto frame = entry.receiver->frame();
            if (frame.isNull()) continue;
            startStream(*pendingStream, entry.sender.label, std::move(entry.receiver), std::move(frame));
            return;
        }
    }

    void startStream(StreamMode &mode, QString name, std::unique_ptr<SharedInput> receiver, QImage firstFrame = {})
    {
        stopSource();
        stopButton->setEnabled(true);
        mode.name->setText(name);
        sharedSourceLabel = QStringLiteral("%1 · %2").arg(mode.label, name);
        status->setText(waitingForSharedSource());
        video.openShared(std::move(receiver), &mode == &sharedMode, std::move(firstFrame));
    }

    void publishFrame(const QImage &source, const QRect &area, const QJsonObject &result)
    {
        if (!syphonOutput && !omtOutput) return;
        QImage output(source.size(), landmarksOnly ? QImage::Format_ARGB32 : QImage::Format_RGB32);
        if (output.isNull()) return;
        output.fill(landmarksOnly ? Qt::transparent : Qt::black);
        {
            QPainter painter(&output);
            if (!landmarksOnly) painter.drawImage(QPoint(), source);
            drawTrackingOverlay(painter, QRectF(area), result, false);
        }
        const auto fps = inferenceRate > 0 ? inferenceRate : frameRate > 0 ? frameRate : 30.0;
        if (syphonOutput) syphonOutput->publish(output, fps);
        if (omtOutput) omtOutput->publish(output, fps);
    }

    void receiveFrame(QImage frame)
    {
        if (frame.isNull()) return;
        if (sourceSize.isEmpty() && !sharedSourceLabel.isEmpty() && status->text() == waitingForSharedSource())
            status->setText(receivingSharedSource());
        if (frameRateClock.isValid())
            ++frameCount;
        else
            frameRateClock.start();
        latest = std::move(frame);
        if (latest.size() != sourceSize) {
            sourceSize = latest.size();
            updateSizeLabel();
        }
        if (!mediapipe.isReady()) showFrame(latest);
    }

    void updateSizeLabel()
    {
        if (sourceSize.isEmpty()) { resolution->setText("No video frame"); return; }
        const auto fps = frameRate >= 0 ? QString::number(frameRate, 'f', 1) : QStringLiteral("—");
        auto text = QStringLiteral("%1 × %2 px  ·  %3 fps").arg(sourceSize.width()).arg(sourceSize.height()).arg(fps);
        if (!analysedSize.isEmpty()) {
            const auto share = 100.0 * analysedSize.width() * analysedSize.height()
                             / (double(sourceSize.width()) * sourceSize.height());
            text += QStringLiteral("  →  analysed %1 × %2 px%3 (%4 % of the source pixels)")
                .arg(analysedSize.width()).arg(analysedSize.height())
                .arg(picture->roi == QRectF(0, 0, 1, 1) ? QString() : QStringLiteral(", ROI only"))
                .arg(share, 0, 'f', share < 10 ? 1 : 0);
        }
        resolution->setText(text);
    }

    QString waitingForSharedSource() const
    {
        return QStringLiteral("Waiting for %1").arg(sharedSourceLabel);
    }

    QString receivingSharedSource() const
    {
        return QStringLiteral("Receiving %1").arg(sharedSourceLabel);
    }

    void stopSource()
    {
        video.stop();
        if (pendingStream) pendingStream->name->setText("Select a running sender");
        pendingStream = nullptr;
        candidates.clear();
        discoveryClock.invalidate();
        if (!sharedSourceLabel.isEmpty()) {
            if (status->text() == waitingForSharedSource() || status->text() == receivingSharedSource())
                status->setText("Ready");
            sharedSourceLabel.clear();
        }
        stopButton->setEnabled(false);
        latest = {};
        active = false;
        sourceSize = analysedSize = {};
        updateSizeLabel();
        telemetry->setText(mediapipe.isReady() ? QStringLiteral("Waiting for input") : analysisStatus);
        frameRateClock.invalidate();
        frameRate = -1;
        frameCount = 0;
        inferenceClock.invalidate();
        inferenceRate = 0;
        inferenceFrames = 0;
        ++sourceGeneration;
        picture->showFrame({});
        signalPanel->resetValues();
    }

    void showFrame(const QImage &image)
    {
        picture->showFrame(image);
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
    QSize sourceSize, analysedSize;
    QComboBox *analysisSize, *cameraQuality;
    int analysisEdge = 640;
    QLineEdit *destinations;
    SignalPanel *signalPanel;
    QPushButton *send, *previewMirror, *previewLandmarks, *stopButton, *startCameraButton;
    pxs::core::tree::Tree tree;
    std::unique_ptr<pxs::OscService> service;
    QButtonGroup *inputModes;
    QStackedWidget *inputPages;
    SliderField *minDetection, *minTracking, *maxHands;
    DetectionOptions detectionOptions;
    QString configurationPath;
    QSplitter *split;
    VideoPreview *picture;
    QLabel *status, *hint;
    QString sharedSourceLabel;
    QString analysisStatus = QStringLiteral("Starting MediaPipe…");
    StreamMode sharedMode, omtMode;
    StreamMode *pendingStream = nullptr;
    std::vector<StreamCandidate> candidates;
    QElapsedTimer discoveryClock, frameRateClock;
    double frameRate = -1;
    int frameCount = 0;
    std::unique_ptr<VideoOutput> syphonOutput, omtOutput;
    bool landmarksOnly = false;
    QAction *filtersAction;
    VideoInput video;
    MediaPipeClient mediapipe;
    QTimer timer;
    QElapsedTimer inferenceClock;
    double inferenceRate = 0;
    int inferenceFrames = 0;
    QImage latest, submitted, submittedSource;
    QRect submittedRoi, previousRoi;
    QByteArray submittedOptions;
    QRectF submittedSelection;
    int sourceGeneration = 0, submittedGeneration = 0;
    int submittedMask = 0;
    bool active = false;
};
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("MpOsc");
    app.setApplicationVersion(QStringLiteral(MPOSC_VERSION));
    app.setWindowIcon(QIcon(":/assets/MpOsc.png"));
    app.setOrganizationName("PixelStereo");
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark); // always dark, whatever the system
#endif
    applyInterfacePalette(app);
    if (app.arguments().contains("--check-mediapipe")) return runSelfTest();
    QSettings settings;
    if (settings.allKeys().isEmpty()) {
        const QSettings legacy("PixelStereo", "MP_OSC");
        for (const auto &key : legacy.allKeys()) settings.setValue(key, legacy.value(key));
    }
    Window window;
    window.show();
    return app.exec();
}
