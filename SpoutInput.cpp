#include "SharedInput.h"

#include "SpoutDX.h"
#include <QByteArray>
#include <wrl/client.h>

namespace {
QImage::Format imageFormat(DXGI_FORMAT format)
{
    switch (format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return QImage::Format_ARGB32;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return QImage::Format_RGBA8888;
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        return QImage::Format_BGR30;
    case DXGI_FORMAT_R16G16B16A16_UNORM:
        return QImage::Format_RGBA64;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return QImage::Format_RGBA16FPx4;
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
        return QImage::Format_RGBA32FPx4;
    default:
        return QImage::Format_Invalid;
    }
}

class SpoutInput final : public SharedInput {
public:
    explicit SpoutInput(const QString &id) : name(id.toLatin1()) {}

    bool open()
    {
        if (!receiver.OpenDirectX11()) return false;
        receiver.SetReceiverName(name.constData());
        return true;
    }

    QImage frame() override
    {
        if (!receiver.ReceiveTexture() || !receiver.IsFrameNew() || name != receiver.GetSenderName())
            return {};
        auto *texture = receiver.GetSenderTexture();
        if (!texture) return {};
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        if (!desc.Width || !desc.Height || desc.Width > 8192 || desc.Height > 8192)
            return {};
        const auto pixelFormat = imageFormat(desc.Format);
        if (pixelFormat == QImage::Format_Invalid) return {};

        if (!staging || width != desc.Width || height != desc.Height || format != desc.Format) {
            desc.BindFlags = 0;
            desc.MiscFlags = 0;
            desc.Usage = D3D11_USAGE_STAGING;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (FAILED(receiver.GetDX11Device()->CreateTexture2D(&desc, nullptr, staging.ReleaseAndGetAddressOf())))
                return {};
            width = desc.Width;
            height = desc.Height;
            format = desc.Format;
        }
        auto *context = receiver.GetDX11Context();
        context->CopyResource(staging.Get(), texture);
        D3D11_MAPPED_SUBRESOURCE pixels{};
        if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &pixels))) return {};
        const auto image = QImage(static_cast<const uchar *>(pixels.pData), int(width), int(height),
                                  qsizetype(pixels.RowPitch), pixelFormat)
                               .convertToFormat(QImage::Format_RGB32);
        context->Unmap(staging.Get(), 0);
        return image;
    }

private:
    QByteArray name;
    spoutDX receiver;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    unsigned width = 0, height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
};
}

QList<SharedSender> sharedSenders()
{
    spoutDX discovery;
    QList<SharedSender> result;
    for (const auto &name : discovery.GetSenderList())
        result.append({QString::fromLatin1(name.data(), qsizetype(name.size())),
                       QString::fromLocal8Bit(name.data(), qsizetype(name.size()))});
    return result;
}

std::unique_ptr<SharedInput> openSharedSender(const QString &id)
{
    auto receiver = std::make_unique<SpoutInput>(id);
    if (!receiver->open()) return {};
    return receiver;
}
