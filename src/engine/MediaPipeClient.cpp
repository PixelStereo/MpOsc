#include "MediaPipeClient.h"

#include "MediaPipeRuntime.h"
#include "Worker.h"

#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QTimer>

MediaPipeClient::MediaPipeClient(QObject *parent) : QObject(parent)
{
    connect(&worker, &QProcess::readyReadStandardOutput, this, [this] {
        while (worker.canReadLine()) {
            const auto result = QJsonDocument::fromJson(worker.readLine()).object();
            if (result.value("ready").toBool()) {
                ready = true;
                emit statusChanged("MediaPipe ready", {});
                continue;
            }
            busy = false;
            for (auto entry = result.begin(); entry != result.end(); ++entry)
                if (entry.key() == "error" || entry.key().endsWith("_error"))
                    emit statusChanged(entry.value().toString(), {});
            emit resultReady(result);
        }
    });
    connect(&worker, &QProcess::errorOccurred, this, [this] { emit statusChanged(worker.errorString(), {}); });
    connect(&worker, &QProcess::readyReadStandardError, this, [this] {
        log = (log + worker.readAllStandardError()).right(4096);
    });
    connect(&worker, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus exitStatus) {
        ready = busy = false;
        if (shuttingDown) return;
        emit statusChanged(exitStatus == QProcess::CrashExit
                               ? QStringLiteral("MediaPipe crashed (%1)").arg(code)
                               : QStringLiteral("MediaPipe exited (%1)").arg(code),
                           QString::fromUtf8(log));
        if (!retried) {
            retried = true;
            QTimer::singleShot(250, this, [this] { start(); });
        }
    });
}

MediaPipeClient::~MediaPipeClient() { shutdown(); }

void MediaPipeClient::start()
{
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("MPLBACKEND", "Agg");
    if (retried || !environment.contains("NYXMAP_MEDIAPIPE_DELEGATE"))
        environment.insert("NYXMAP_MEDIAPIPE_DELEGATE", "cpu");
    worker.setProcessEnvironment(environment);
    const auto root = runtimeFolder();
    worker.start(pythonExecutable(root), {"-I", "-u", "-c", QString::fromUtf8(workerScript), root});
}

void MediaPipeClient::shutdown()
{
    shuttingDown = true;
    worker.closeWriteChannel();
    if (worker.state() != QProcess::NotRunning && !worker.waitForFinished(300)) {
        worker.kill();
        worker.waitForFinished(1000);
    }
}

void MediaPipeClient::send(const QByteArray &packet)
{
    busy = true;
    worker.write(packet);
}
