#include "MediaPipeRuntime.h"

#include <QCoreApplication>
#include <QDir>

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
