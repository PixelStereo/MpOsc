#include "VideoOutput.h"

#import <Syphon/SyphonOpenGLServer.h>
#import <Syphon/SyphonServerDirectory.h>
#import <OpenGL/gl.h>

namespace {
class SyphonOutput final : public VideoOutput {
public:
    SyphonOutput(SyphonOpenGLServer *server, CGLContextObj context)
        : server(server), context(context) {}

    ~SyphonOutput() override
    {
        [server stop];
        const auto previous = CGLGetCurrentContext();
        if (CGLSetCurrentContext(context) == kCGLNoError) {
            if (texture) glDeleteTextures(1, &texture);
            CGLSetCurrentContext(previous);
        }
        CGLReleaseContext(context);
    }

    QString senderId() const override
    {
        NSString *uuid = (NSString *)server.serverDescription[SyphonServerDescriptionUUIDKey];
        return uuid ? QString::fromNSString(uuid) : QString();
    }

    void publish(const QImage &image, double) override
    {
        @autoreleasepool {
            const auto previous = CGLGetCurrentContext();
            if (CGLSetCurrentContext(context) != kCGLNoError) return;
            if (!texture) glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_RECTANGLE_ARB, texture);
            if (size != image.size()) {
                glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8, image.width(), image.height(), 0,
                             GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, nullptr);
                size = image.size();
            }
            glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, image.width(), image.height(),
                            GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, image.constBits());
            [server publishFrameTexture:texture textureTarget:GL_TEXTURE_RECTANGLE_ARB
                imageRegion:NSMakeRect(0, 0, image.width(), image.height())
                textureDimensions:NSMakeSize(image.width(), image.height()) flipped:YES];
            CGLSetCurrentContext(previous);
        }
    }

private:
    SyphonOpenGLServer *server;
    CGLContextObj context;
    GLuint texture = 0;
    QSize size;
};
}

std::unique_ptr<VideoOutput> openSyphonOutput(const QString &name)
{
    @autoreleasepool {
        CGLPixelFormatObj format = nullptr;
        GLint count = 0;
        const CGLPixelFormatAttribute attributes[] = {kCGLPFAAccelerated, CGLPixelFormatAttribute(0)};
        if (CGLChoosePixelFormat(attributes, &format, &count) != kCGLNoError || !format) return {};
        CGLContextObj context = nullptr;
        const auto error = CGLCreateContext(format, nullptr, &context);
        CGLReleasePixelFormat(format);
        if (error != kCGLNoError || !context) return {};
        auto *server = [[SyphonOpenGLServer alloc] initWithName:name.toNSString() context:context options:nil];
        if (server) return std::make_unique<SyphonOutput>(server, context);
        CGLReleaseContext(context);
        return {};
    }
}
