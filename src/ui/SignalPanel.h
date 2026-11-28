#pragma once

#include "SignalFilters.h"
#include <pxs/Devices.h>
#include <pxs/DmxOut.h>

#include <QElapsedTimer>
#include <QJsonObject>
#include <QLineEdit>
#include <QMap>
#include <QSettings>
#include <QString>
#include <QTreeWidget>
#include <QUdpSocket>
#include <QVariant>
#include <QWidget>
#include <functional>

class FamilyItem;
class QCheckBox;
namespace pxs::filterview {
class FilterEditor;
class SignalMeter;
}

// The signal tables (pinned, then the others by family), a chain of filters per signal edited at the right of them, and
// the OSC output of what the chains give.
class SignalPanel : public QWidget {
public:
    explicit SignalPanel(QWidget *parent = nullptr, const QString &deviceName = QStringLiteral("MpOsc"));

    QString filter() const;
    void setFilter(const QString &text);
    // Rows saved in the settings are created even before a signal first appears.
    void restoreRows(const QSettings &settings);
    void restoreDevices(const QSettings &settings);
    void clear();
    // Whether OSC is armed: meters of enabled signals light up accordingly.
    void setSending(bool enabled);
    void resetSent();
    void resetValues();
    // Filters and sends the signals of one MediaPipe result; returns the status line to show.
    QString update(const QJsonObject &frame, const QString &destinations, bool sendEnabled);
    // Told of every signal the panel knows, once its row exists (what the OSCQuery namespace lists)
    void setSignalKnown(std::function<void(const QString &address)> callback);
    // Told each value a live signal takes (after its filters, a bool, an int or a double as the signal is), whether or not it is sent:
    // what listens to the namespace follows it
    void setSignalFired(std::function<void(const QString &address, const QVariant &value)> callback) { signalFired = std::move(callback); }
    // The chain of a signal, as it is now
    FilterChain chainOf(const QString &address) const;
    // The detail of a signal: its chain of filters, with the signal before and after each filter. Hidden until asked (Ctrl+M)
    void setFiltersShown(bool shown);
    bool filtersShown() const;
    void selectSignal(const QString &address) { select(address); }
    // Gives a signal a chain of filters (saved), whoever makes it
    void setChainOf(const QString &address, FilterChain chain);

private:
    struct Row {
        QTreeWidgetItem *item = nullptr;
        pxs::filterview::SignalMeter *meter = nullptr;
        QCheckBox *send = nullptr;
        SignalFilter filter;
        bool pinned = false, initialized = false, sent = false;
        bool deviceSent = false;
        quint64 device = 0;
        QString target;
        int level = 0;
        QString raw = QStringLiteral("—"), shown = QStringLiteral("—");
    };

    void resizePinnedTable();
    void moveSignal(const QString &address, bool pinned);
    Row makeRow(const QString &address);
    void buildItem(const QString &address, Row &row);
    void removeItem(Row &row);
    FamilyItem *familyItem(const QString &family);
    void applyQuery();
    void select(const QString &address);
    void setChain(const QString &address, FilterChain chain, bool save);
    void showBadge(Row &row);
    void familyMenu(const QString &family, const QPoint &globalPosition);

    void showDevices();
    void signalTargetMenu(const QString &address, const QPoint &position);
    void updateDeviceReferences();
    void submitDmx();

    pxs::devices::Devices devices;
    pxs::dmx::Sender dmx;
    std::vector<pxs::dmx::Output> dmxOutputs;
    QLineEdit *search;
    QTreeWidget *table, *pinnedTable;
    pxs::filterview::FilterEditor *editor;
    QMap<QString, Row> rows;
    QMap<QString, FamilyItem *> families;
    QString query, selected;
    QUdpSocket udp;
    QElapsedTimer clock;
    std::function<void(const QString &)> signalKnown;
    std::function<void(const QString &, const QVariant &)> signalFired;
    bool sending = false;
};
