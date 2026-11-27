#include "VideoInput.h"

#include <QCameraFormat>
#include <QMediaDevices>
#include <QUrl>
#include <QVideoFrame>

VideoInput::VideoInput(QObject *parent) : QObject(parent)
{
    capture.setVideoSink(&cameraSink);
    player.setVideoSink(&fileSink);
    player.setLoops(QMediaPlayer::Infinite);
    connect(&cameraSink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
        if (current == Source::Camera && frame.isValid()) emit frameReady(frame.toImage());
    });
    connect(&fileSink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
        if (current == Source::File && frame.isValid()) emit frameReady(frame.toImage());
    });
    connect(&player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString &error) { emit errorOccurred(error); });
}

bool VideoInput::startCamera(const QByteArray &deviceId, qint64 pixelLimit)
{
    for (const auto &device : QMediaDevices::videoInputs()) {
        if (device.id() != deviceId) continue;
        stop();
        capture.setCamera(nullptr);
        camera.reset(new QCamera(device));
        // The largest landscape format within the limit; when every format is larger, the smallest one.
        QCameraFormat preferred;
        bool preferredWithinLimit = false;
        for (const auto &format : device.videoFormats()) {
            const auto size = format.resolution();
            if (size.width() <= size.height()) continue;
            const qint64 pixels = qint64(size.width()) * size.height();
            const bool within = pixelLimit == 0 || pixels <= pixelLimit;
            const auto best = preferred.resolution();
            const qint64 bestPixels = qint64(best.width()) * best.height();
            if (preferred.isNull() || (within && (!preferredWithinLimit || pixels > bestPixels))
                || (!within && !preferredWithinLimit && pixels < bestPixels)) {
                preferred = format;
                preferredWithinLimit = within;
            }
        }
        if (!preferred.isNull()) camera->setCameraFormat(preferred);
        connect(camera.get(), &QCamera::errorOccurred, this,
                [this](QCamera::Error, const QString &error) { emit errorOccurred(error); });
        capture.setCamera(camera.get());
        current = Source::Camera;
        camera->start();
        return true;
    }
    return false;
}

void VideoInput::openFile(const QString &path)
{
    stop();
    current = Source::File;
    player.setSource(QUrl::fromLocalFile(path));
    player.play();
}

void VideoInput::openShared(std::unique_ptr<SharedInput> receiver, bool allowFlip, QImage firstFrame)
{
    stop();
    shared = std::move(receiver);
    current = Source::Shared;
    flipAllowed = allowFlip;
    receiveSharedFrame(std::move(firstFrame));
}

void VideoInput::stop()
{
    current = Source::None;
    shared.reset();
    if (camera) camera->stop();
    player.stop();
}

void VideoInput::poll()
{
    if (shared) receiveSharedFrame(shared->frame());
}

void VideoInput::receiveSharedFrame(QImage frame)
{
    if (frame.isNull()) return;
    if (flip && flipAllowed) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        frame.flip(Qt::Vertical);
#else
        frame = frame.mirrored(false, true);
#endif
    }
    emit frameReady(frame);
}

void VideoInput::setSharedFlip(bool enabled)
{
    flip = enabled;
    if (current == Source::Shared && flipAllowed) emit sourceReset();
}
