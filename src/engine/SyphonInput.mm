#include "SharedInput.h"

#import <Syphon/SyphonMetalClient.h>
#import <Syphon/SyphonServerDirectory.h>

namespace {
NSDictionary *serverWithId(const QString &id)
{
    for (NSDictionary *server in [[SyphonServerDirectory sharedDirectory] servers])
        if ([server[SyphonServerDescriptionUUIDKey] isEqualToString:id.toNSString()]) return server;
    return nil;
}

class SyphonInput final : public SharedInput {
public:
    SyphonInput(const QString &sender, id<MTLDevice> device)
        : senderId(sender), device(device), queue([device newCommandQueue]) {}

    ~SyphonInput() override { [client stop]; }

    QImage frame() override
    {
        @autoreleasepool {
            if (client && !client.isValid) {
                [client stop];
                client = nil;
            }
            if (!client) {
                NSDictionary *server = serverWithId(senderId);
                if (!server) return {};
                client = [[SyphonMetalClient alloc] initWithServerDescription:server
                    device:device options:nil newFrameHandler:nil];
            }
            if (!client.hasNewFrame) return {};
            id<MTLTexture> texture = [client newFrameImage];
            if (!texture || !texture.width || !texture.height || texture.width > 8192 || texture.height > 8192)
                return {};
            const NSUInteger stride = (texture.width * 4 + 255) & ~NSUInteger(255);
            const NSUInteger bytes = stride * texture.height;
            if (buffer.length < bytes)
                buffer = [device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
            if (!buffer) return {};

            id<MTLCommandBuffer> command = [queue commandBuffer];
            id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
            [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                sourceSize:MTLSizeMake(texture.width, texture.height, 1) toBuffer:buffer
                destinationOffset:0 destinationBytesPerRow:stride destinationBytesPerImage:bytes];
            [blit endEncoding];
            [command commit];
            [command waitUntilCompleted];
            if (command.status != MTLCommandBufferStatusCompleted) return {};
            // Read the BGRA surface in sender order. The source menu controls vertical orientation.
            return QImage(static_cast<const uchar *>(buffer.contents), int(texture.width), int(texture.height),
                          qsizetype(stride), QImage::Format_ARGB32).convertToFormat(QImage::Format_RGB32);
        }
    }

private:
    QString senderId;
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    SyphonMetalClient *client = nil;
    id<MTLBuffer> buffer = nil;
};
}

QList<SharedSender> sharedSenders()
{
    QList<SharedSender> result;
    @autoreleasepool {
        for (NSDictionary *server in [[SyphonServerDirectory sharedDirectory] servers]) {
            NSString *uuid = server[SyphonServerDescriptionUUIDKey];
            if (!uuid) continue;
            NSString *application = server[SyphonServerDescriptionAppNameKey];
            NSString *name = server[SyphonServerDescriptionNameKey];
            const QString app = application ? QString::fromNSString(application) : QString();
            const QString feed = name ? QString::fromNSString(name) : QString();
            const QString label = app.isEmpty() ? feed : feed.isEmpty() ? app : app + " — " + feed;
            result.append({QString::fromNSString(uuid), label.isEmpty() ? QStringLiteral("Syphon") : label});
        }
    }
    return result;
}

std::unique_ptr<SharedInput> openSharedSender(const QString &sender)
{
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device || !serverWithId(sender)) return {};
        return std::make_unique<SyphonInput>(sender, device);
    }
}
