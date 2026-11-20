#pragma once

#include "SignalFilters.h"

#include <QWidget>
#include <vector>

class QLabel;
class QListWidget;
class QPushButton;
class QToolButton;

// The detail of one signal: its chain of filters. "Add filter" puts one at the end, a filter is folded to its name, its
// settings follow a drag to another place of the list, and every change is told to whoever holds the chain.
class FilterEditor : public QWidget {
    Q_OBJECT
public:
    explicit FilterEditor(QWidget *parent = nullptr);

    // Shows the chain of a signal (an empty address: no signal selected)
    void setSignal(const QString &address, const FilterChain &chain);
    QString address() const { return signalAddress; }
    // The live value of the signal, before and after its chain
    void setLive(const QString &input, const QString &output);

signals:
    // `final`: the end of a change (a drag of a setting is not)
    void chainChanged(const QString &address, const FilterChain &chain, bool final);

private:
    void rebuild();
    void addCard(int index);
    void edited(bool final);
    void fitItems();
    void addFilter(InteractionFilter kind);

    QString signalAddress;
    FilterChain chain;
    std::vector<bool> open; // which cards are unfolded, in the order of the chain
    QLabel *title, *type, *live, *empty;
    QPushButton *add;
    QToolButton *clear;
    QListWidget *list;
    bool rebuilding = false;
};
