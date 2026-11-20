#pragma once

#include "Signals.h"

#include <Interactions.h>
#include <QString>
#include <QVariant>
#include <optional>
#include <vector>

// A signal's chain of filters: the interaction filters of pxs (Attack Release, Average, Hold, Math…), one after the other,
// whatever the type of the signal is. A switch goes in as 0 or 1 and comes out on from 0.5, a count comes out rounded.
using FilterChain = std::vector<InteractionStage>;

// The filters that mean something for this signal (the filters over time suit every type; the others, the type they suit)
std::vector<InteractionFilter> filtersOffered(const QString &address);
// What "Invert" is for this signal: 1 − x for a switch or a measure of 0 to 1, −x for an angle or a depth, 2 − x for the hands count
FilterChain invertChain(const QString &address);
// An angle (head yaw, pitch, roll) is in degrees, not in 0 to 1
bool isAngle(const QString &address);

// A chain as saved in the settings (JSON in one value); a text that is not one gives no filter
QString chainToText(const FilterChain &chain);
FilterChain chainFromText(const QString &text);

// What a filtered value is for the signal's type, as OSC and the namespace say it: a bool, an int, a double
QVariant signalValue(SignalType type, double value);

// One signal's chain, running: its value goes in, the filtered value comes out
class SignalFilter {
public:
    void setChain(FilterChain chain) { it.filters = std::move(chain); }
    const FilterChain &chain() const { return it.filters; }
    // A new value of the signal, `dt` seconds after the last; the value after the chain
    double step(double raw, double dt);
    void reset() { processor.reset(); }

private:
    Interaction it;
    InteractionProcessor processor;
};
