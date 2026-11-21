#pragma once

#include "SignalFilters.h"

#include <QWidget>
#include <functional>
#include <vector>

class QMenu;
// The filters in a menu: by group, each with what it does in a line and, in its tooltip, when to use it
void addFilterActions(QMenu *menu, const std::vector<InteractionFilter> &kinds, const std::function<void(InteractionFilter)> &chosen);

class QLabel;
class QListWidget;
class QProgressBar;
class SignalMeter;
class QPushButton;
class QToolButton;

// The detail of one signal: its chain of filters. "Add filter" puts one at the end, a filter is folded to its name, its
// settings follow a drag to another place of the list, and every change is told to whoever holds the chain. A bar over the
// first filter shows the signal as it comes in and one under each filter shows it after that filter, to compare them.
class FilterEditor : public QWidget {
    Q_OBJECT
public:
    explicit FilterEditor(QWidget *parent = nullptr);

    // Shows the chain of a signal (an empty address: no signal selected)
    void setSignal(const QString &address, const FilterChain &chain);
    QString address() const { return signalAddress; }
    // The live value of the signal as it comes in, and after each filter of its chain (nothing when the signal is not there)
    void setTrace(bool available, double input, const std::vector<double> &afterEach);

signals:
    // `final`: the end of a change (a drag of a setting is not)
    void chainChanged(const QString &address, const FilterChain &chain, bool final);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void rebuild();
    void addCard(int index);
    void edited(bool final);
    void fitItems();
    void addFilter(InteractionFilter kind);

    QString signalAddress;
    FilterChain chain;
    std::vector<bool> open; // which cards are unfolded, in the order of the chain
    QLabel *title;
    QProgressBar *input;
    SignalMeter *output; // after the last filter: what is sent, with the incoming signal marked on it
    bool angle = false;
    SignalType kind = SignalType::Float;
    QPushButton *add;
    QToolButton *clear;
    QListWidget *list;
    bool rebuilding = false;
};
