#pragma once

#include <QImage>
#include <QString>
#include <memory>

class VideoOutput {
public:
    virtual ~VideoOutput() = default;
    virtual QString senderId() const = 0;
    virtual void publish(const QImage &image, double fps) = 0;
};

std::unique_ptr<VideoOutput> openOmtOutput(const QString &name);
#ifdef Q_OS_MACOS
std::unique_ptr<VideoOutput> openSyphonOutput(const QString &name);
#elif defined(Q_OS_WIN)
std::unique_ptr<VideoOutput> openSpoutOutput(const QString &name);
#endif
