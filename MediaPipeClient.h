#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QString>

// Owns the Python MediaPipe worker: starts it (once more on failure, on CPU), sends one frame at a time and
// reports each reply. Nothing here knows about the interface.
class MediaPipeClient : public QObject {
    Q_OBJECT
public:
    explicit MediaPipeClient(QObject *parent = nullptr);
    ~MediaPipeClient() override;

    void start();
    // Stops the worker without reporting its exit.
    void shutdown();
    bool isReady() const { return ready; }
    // True from send() until the reply arrives: only one frame is in flight.
    bool isBusy() const { return busy; }
    void send(const QByteArray &packet);

signals:
    void statusChanged(const QString &text, const QString &details);
    void resultReady(const QJsonObject &result);

private:
    QProcess worker;
    QByteArray log;
    bool ready = false, busy = false, retried = false, shuttingDown = false;
};
