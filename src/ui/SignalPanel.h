#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QLineEdit>
#include <QMap>
#include <QSettings>
#include <QString>
#include <QTreeWidget>
#include <QUdpSocket>
#include <QWidget>
#include <functional>

class QCheckBox;
class QLabel;
class QProgressBar;
class QPushButton;
class SliderField;

// The signal tables (pinned and others), their per-signal Invert / Attack / Release settings and the OSC output.
class SignalPanel : public QWidget {
public:
    explicit SignalPanel(QWidget *parent = nullptr);

    QString filter() const;
    void setFilter(const QString &text);
    // Rows saved in the settings are created even before a signal first appears.
    void restoreRows(const QSettings &settings);
    void clear();
    // Whether OSC is armed: meters of enabled signals light up accordingly.
    void setSending(bool enabled);
    void resetSent();
    void resetValues();
    // Smooths and sends the signals of one MediaPipe result; returns the status line to show.
    QString update(const QJsonObject &frame, const QString &destinations, bool sendEnabled);
    // Told of every signal the panel knows, once its row exists (what the OSCQuery namespace lists)
    void setSignalKnown(std::function<void(const QString &address)> callback);

private:
    struct Row {
        QTreeWidgetItem *item;
        QProgressBar *meter;
        QLabel *raw;
        QCheckBox *invert;
        QPushButton *signal;
        SliderField *attack, *release;
        double filtered = 0;
        bool initialized = false, sent = false;
    };

    void resizePinnedTable();
    void moveSignal(const QString &address, bool pinned);
    Row makeRow(const QString &address);

    QLineEdit *search;
    QTreeWidget *table, *pinnedTable;
    QMap<QString, Row> rows;
    QString query;
    QUdpSocket udp;
    QElapsedTimer clock;
    std::function<void(const QString &)> signalKnown;
    bool sending = false;
};
