#pragma once

#include "SharedInput.h"

#include <QCamera>
#include <QImage>
#include <QMediaCaptureSession>
#include <QMediaPlayer>
#include <QObject>
#include <QVideoSink>
#include <memory>

// The one active video source (camera, movie file or Syphon/Spout sender) and its frames. It knows nothing
// about the interface: callers pick the source and listen to frameReady().
class VideoInput : public QObject {
    Q_OBJECT
public:
    enum class Source { None, Camera, File, Shared };

    explicit VideoInput(QObject *parent = nullptr);

    Source source() const { return current; }
    // pixelLimit: the largest camera format to request, in pixels (0 = no limit). False when the device is unknown.
    bool startCamera(const QByteArray &deviceId, qint64 pixelLimit);
    void openFile(const QString &path);
    void openShared(std::unique_ptr<SharedInput> receiver);
    void stop();

    // Shared senders are polled rather than pushed: call this at the pace frames are wanted.
    void poll();
    bool sharedFlip() const { return flip; }
    // Flips Syphon/Spout frames vertically (their origin is bottom-left on some platforms).
    void setSharedFlip(bool enabled);

signals:
    void frameReady(const QImage &frame);
    void errorOccurred(const QString &message);
    // The picture changes meaning (flip toggled on the live shared source): restart tracking.
    void sourceReset();

private:
    QMediaCaptureSession capture;
    QMediaPlayer player;
    QVideoSink cameraSink, fileSink;
    std::unique_ptr<QCamera> camera;
    std::unique_ptr<SharedInput> shared;
    Source current = Source::None;
    bool flip = false;
};
