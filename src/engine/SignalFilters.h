#pragma once

#include "Signals.h"

#include <Interactions.h>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <memory>
#include <optional>
#include <vector>

// A signal's chain of filters: the interaction filters of pxs (Attack Release, Average, Hold, Math…), one after the other,
// whatever the type of the signal is. A switch goes in as 0 or 1 and comes out on from 0.5, a count comes out rounded.
using FilterChain = std::vector<InteractionStage>;

// The filters that mean something for this signal (the filters over time suit every type; the others, the type they suit)
std::vector<InteractionFilter> filtersOffered(const QString &address);
// The mini documentation of a filter, to choose one: its group (Smooth, Time, Trigger, Shape), what it does in a line, and when to use it
struct FilterInfo {
    QString group, summary, detail;
};
FilterInfo filterInfo(InteractionFilter kind);
QStringList filterGroups(); // in the order the menus list them

// What "Invert" is for this signal: 1 − x for a switch or a measure of 0 to 1, −x for an angle or a depth, 2 − x for the hands count
FilterChain invertChain(const QString &address);
// An angle (head yaw, pitch, roll) is in degrees, not in 0 to 1
bool isAngle(const QString &address);

// A chain as saved in the settings (JSON in one value); a text that is not one gives no filter
QString chainToText(const FilterChain &chain);
FilterChain chainFromText(const QString &text);

// What a filtered value is for the signal's type, as OSC and the namespace say it: a bool, an int, a double
QVariant signalValue(SignalType type, double value);

// The text of a value of a signal: on / off, a count, a measure (in degrees for an angle)
QString signalText(SignalType type, bool angle, double value);
// Where a value is on a bar that runs from `minimum` to `maximum` (0 to 1000 for a measure, 0 to 2 for a count, the degrees of an angle)
int signalLevel(SignalType type, bool angle, int minimum, int maximum, double value);

// One signal's chain, running: its value goes in, the filtered value comes out
class SignalFilter {
public:
    void setChain(FilterChain chain)
    {
        it.filters = std::move(chain);
        if (trace) rebuildTrace();
    }
    const FilterChain &chain() const { return it.filters; }
    // A new value of the signal, `dt` seconds after the last; the value after the chain
    double step(double raw, double dt);
    void reset();
    // The value after each filter of the chain (one more pass of the chain per filter): wanted for the signal being looked at only
    void setTraced(bool on);
    bool traced() const { return trace; }
    const std::vector<double> &afterEach() const { return stages; }

private:
    void rebuildTrace();
    struct Prefix { // the chain up to one filter, running on the same values
        Interaction it;
        InteractionProcessor processor;
    };
    Interaction it;
    InteractionProcessor processor;
    bool trace = false;
    std::vector<std::shared_ptr<Prefix>> prefixes;
    std::vector<double> stages;
};
