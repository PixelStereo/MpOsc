#include "VideoOutput.h"

#include "SpoutDX.h"
#include <QByteArray>

namespace {
class SpoutOutput final : public VideoOutput {
public:
    bool open(const QString &name)
    {
        if (!sender.OpenDirectX11() || !sender.SetSenderName(name.toLatin1().constData()))
            return false;
        sender.SetSenderFormat(DXGI_FORMAT_B8G8R8A8_UNORM);
        return true;
    }

    QString senderId() const override
    {
        return QString::fromLatin1(sender.GetName());
    }

    void publish(const QImage &image, double) override
    {
        sender.SendImage(image.constBits(), image.width(), image.height(), image.bytesPerLine());
    }

private:
    mutable spoutDX sender;
};
}

std::unique_ptr<VideoOutput> openSpoutOutput(const QString &name)
{
    auto output = std::make_unique<SpoutOutput>();
    if (!output->open(name)) return {};
    return output;
}
