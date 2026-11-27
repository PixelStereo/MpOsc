#include "SharedInput.h"
#include "VideoOutput.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QLibrary>
#include <QtMath>
#include <cstdint>

namespace {
// C ABI of libomt.h from the Open Media Transport SDK.
struct MediaFrame {
    int type;
    int64_t timestamp;
    int codec, width, height, stride, flags, frameRateN, frameRateD;
    float aspectRatio;
    int colorSpace, sampleRate, channels, samplesPerChannel;
    void *data;
    int dataLength;
    void *compressedData;
    int compressedLength;
    void *metadata;
    int metadataLength;
};

constexpr int video = 2;
constexpr int bgra = 0x41524742;
constexpr int preferredBgra = 2;
constexpr int alpha = 2;

class OmtLibrary {
public:
    OmtLibrary()
    {
        QDir folder(QCoreApplication::applicationDirPath());
#ifdef Q_OS_MACOS
        folder.cd("../Frameworks");
        codec.setFileName(folder.filePath("libvmx.dylib"));
        library.setFileName(folder.filePath("libomt.dylib"));
#elif defined(Q_OS_WIN)
        codec.setFileName(folder.filePath("libvmx.dll"));
        library.setFileName(folder.filePath("libomt.dll"));
#else
        return;
#endif
        codec.setLoadHints(QLibrary::ExportExternalSymbolsHint | QLibrary::PreventUnloadHint);
        library.setLoadHints(QLibrary::PreventUnloadHint);
        if (!codec.load() || !library.load()) return;
        resolve(discover, "omt_discovery_getaddresses");
        resolve(openReceiver, "omt_receive_create");
        resolve(closeReceiver, "omt_receive_destroy");
        resolve(receive, "omt_receive");
        resolve(openSender, "omt_send_create");
        resolve(closeSender, "omt_send_destroy");
        resolve(address, "omt_send_getaddress");
        resolve(send, "omt_send");
    }

    char **(*discover)(int *) = nullptr;
    void *(*openReceiver)(const char *, int, int, int) = nullptr;
    void (*closeReceiver)(void *) = nullptr;
    MediaFrame *(*receive)(void *, int, int) = nullptr;
    void *(*openSender)(const char *, int) = nullptr;
    void (*closeSender)(void *) = nullptr;
    int (*address)(void *, char *, int) = nullptr;
    int (*send)(void *, MediaFrame *) = nullptr;

private:
    template<class Function> void resolve(Function &function, const char *name)
    {
        function = reinterpret_cast<Function>(library.resolve(name));
    }
    QLibrary codec, library;
};

const OmtLibrary &omt()
{
    static const OmtLibrary library;
    return library;
}

class OmtInput final : public SharedInput {
public:
    explicit OmtInput(void *receiver) : receiver(receiver) {}
    ~OmtInput() override { omt().closeReceiver(receiver); }

    QImage frame() override
    {
        const auto *frame = omt().receive(receiver, video, 0);
        if (!frame || frame->type != video || frame->codec != bgra || !frame->data ||
            frame->width <= 0 || frame->height <= 0 || frame->width > 8192 || frame->height > 8192 ||
            frame->stride < frame->width * 4 || frame->dataLength < qint64(frame->stride) * frame->height)
            return {};
        const auto format = frame->flags & alpha ? QImage::Format_ARGB32 : QImage::Format_RGB32;
        return QImage(static_cast<const uchar *>(frame->data), frame->width, frame->height, frame->stride, format).copy();
    }

private:
    void *receiver;
};

class OmtOutput final : public VideoOutput {
public:
    explicit OmtOutput(void *sender) : sender(sender) {}
    ~OmtOutput() override { omt().closeSender(sender); }

    QString senderId() const override
    {
        char buffer[1024]{};
        omt().address(sender, buffer, sizeof buffer);
        return QString::fromUtf8(buffer, qstrnlen(buffer, sizeof buffer));
    }

    void publish(const QImage &image, double fps) override
    {
        MediaFrame frame{};
        frame.type = video;
        frame.timestamp = clock.nsecsElapsed() / 100;
        frame.codec = bgra;
        frame.width = image.width();
        frame.height = image.height();
        frame.stride = int(image.bytesPerLine());
        frame.flags = image.hasAlphaChannel() ? alpha : 0;
        frame.frameRateN = qRound(fps * 1000);
        frame.frameRateD = 1000;
        frame.aspectRatio = float(frame.width) / frame.height;
        frame.colorSpace = 709;
        frame.data = const_cast<uchar *>(image.constBits());
        frame.dataLength = int(image.sizeInBytes());
        omt().send(sender, &frame);
    }

private:
    void *sender;
    QElapsedTimer clock = [] { QElapsedTimer timer; timer.start(); return timer; }();
};
}

bool omtAvailable()
{
    return omt().discover && omt().openReceiver && omt().closeReceiver && omt().receive;
}

QList<SharedSender> omtSenders()
{
    QList<SharedSender> result;
    if (!omtAvailable()) return result;
    int count = 0;
    auto addresses = omt().discover(&count);
    for (int i = 0; addresses && i < count; ++i) {
        const auto address = QString::fromUtf8(addresses[i]);
        if (!address.isEmpty()) result.append({address, address});
    }
    return result;
}

std::unique_ptr<SharedInput> openOmtSender(const QString &id)
{
    if (!omtAvailable()) return {};
    auto receiver = omt().openReceiver(id.toUtf8().constData(), video, preferredBgra, 0);
    if (!receiver) return {};
    return std::make_unique<OmtInput>(receiver);
}

std::unique_ptr<VideoOutput> openOmtOutput(const QString &name)
{
    if (!omt().openSender || !omt().closeSender || !omt().address || !omt().send) return {};
    auto sender = omt().openSender(name.toUtf8().constData(), 0);
    if (!sender) return {};
    return std::make_unique<OmtOutput>(sender);
}
