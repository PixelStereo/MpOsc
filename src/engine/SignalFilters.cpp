#include "SignalFilters.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

bool isAngle(const QString &address) { return address.startsWith(QLatin1String("/face/head/")); }

std::vector<InteractionFilter> filtersOffered(const QString &address)
{
    using F = InteractionFilter;
    // Over time, for every type
    std::vector<F> offered = {F::Delay, F::Hold, F::Debounce, F::AutoReturn, F::Average, F::Envelope};
    const auto type = signalType(address);
    if (type == SignalType::Bool) {
        offered.push_back(F::Toggle);
        return offered;
    }
    offered.insert(offered.end(), {F::WeightedAverage, F::AttackRelease});
    if (type == SignalType::Float) offered.insert(offered.end(), {F::Damper, F::PulseDetection, F::Speed});
    offered.push_back(F::Math);
    // These work in 0 to 1: not for an angle or a count
    if (type == SignalType::Float && !isAngle(address)) offered.insert(offered.end(), {F::Remap, F::Clamp, F::Steps, F::Threshold, F::Toggle});
    return offered;
}

FilterChain invertChain(const QString &address)
{
    auto stage = [](MathOp op, double value) {
        auto s = InteractionStage::made(InteractionFilter::Math);
        s.name = QStringLiteral("Invert");
        s.op = op;
        s.a = value;
        return s;
    };
    const auto type = signalType(address);
    if (type == SignalType::Int) return {stage(MathOp::Multiply, -1), stage(MathOp::Add, 2)};
    if (isAngle(address) || address.endsWith(QLatin1String("/z"))) return {stage(MathOp::Multiply, -1)};
    return {stage(MathOp::OneMinus, 0)};
}

QString chainToText(const FilterChain &chain)
{
    if (chain.empty()) return {};
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"filters", Interaction::filtersToJson(chain)}}).toJson(QJsonDocument::Compact));
}

FilterChain chainFromText(const QString &text)
{
    if (text.isEmpty()) return {};
    return Interaction::filtersFromJson(QJsonDocument::fromJson(text.toUtf8()).object());
}

QVariant signalValue(SignalType type, double value)
{
    if (type == SignalType::Bool) return value >= 0.5;
    if (type == SignalType::Int) return int(std::lround(value));
    return value;
}

double SignalFilter::step(double raw, double dt)
{
    processor.input(it, raw);
    return processor.step(it, dt).value_or(raw);
}

QStringList filterGroups() { return {QStringLiteral("Smooth"), QStringLiteral("Time"), QStringLiteral("Trigger"), QStringLiteral("Shape")}; }

FilterInfo filterInfo(InteractionFilter kind)
{
    using F = InteractionFilter;
    const auto make = [](const char *group, const char *summary, const char *detail) {
        return FilterInfo{QString::fromUtf8(group), QString::fromUtf8(summary), QString::fromUtf8(detail)};
    };
    switch (kind) {
    case F::Average: return make("Smooth", "mean of the last moments", "Calms a jittery signal: the longer the window, the smoother and the later. On a switch it is a vote: on when it was on most of the window.");
    case F::WeightedAverage: return make("Smooth", "smooths, reacts at once", "A gentle smoothing that starts moving immediately. The setting is about the time it takes to arrive. The first one to try on an expression.");
    case F::AttackRelease: return make("Smooth", "its own speed up and down", "Rises and falls at different speeds: quick to rise and slow to fall makes a blink or a smile linger. Good for expressions and gestures.");
    case F::Damper: return make("Smooth", "a spring, may overshoot", "Follows like a spring: lively and natural. Damping 1 never overshoots, lower swings past the value.");
    case F::Delay: return make("Time", "the value of N seconds ago", "Waits before passing the value on. Works on switches and counts: a gesture that starts a second later.");
    case F::Hold: return make("Time", "keeps a fall for N seconds", "A rise goes through at once, a fall waits: a switch stays on a moment after it went off, a measure holds its peak. Keeps a lost hand from flickering.");
    case F::Debounce: return make("Time", "ignores what does not last", "A change is followed only once it has stayed for N seconds. Removes the flicker of a switch or a count that the tracking hesitates on.");
    case F::AutoReturn: return make("Time", "back to rest after N seconds", "Goes back to off (or 0, or the value you set) after a while, even if the signal stays up. It is armed again when the signal is at rest.");
    case F::Envelope: return make("Trigger", "a hit becomes a fading pulse", "Each rise starts an attack / decay / sustain / release shape. Turns a gesture or a blink into a flash that fades by itself.");
    case F::PulseDetection: return make("Trigger", "a pulse at each sudden rise", "Fires 1 when the value jumps up quickly, then decays. Reacts to the movement, not to the level.");
    case F::Toggle: return make("Trigger", "flips on / off at each rise", "Each time the value goes up past the threshold the output changes: on, then off at the next one. A gesture as a push button.");
    case F::Threshold: return make("Trigger", "on or off around a level", "Gives 0 or 1 depending on a level, with a margin so it does not flicker around it.");
    case F::Math: return make("Shape", "add, multiply, round, power…", "An operation on the value: x + a, x × a, absolute value, 1 − x, rounding to a step…");
    case F::Remap: return make("Shape", "one range onto another", "Maps In Min..In Max to Out Min..Out Max. Use it to widen a small range (a mouth that barely opens) to 0..1.");
    case F::Clamp: return make("Shape", "keeps between two values", "Cuts what is below Min or above Max.");
    case F::Steps: return make("Shape", "only a few levels", "Rounds the value to N levels between 0 and 1: a staircase instead of a slope.");
    case F::Speed: return make("Shape", "how fast it moves", "Gives 0..1 from the speed of the signal, whatever its level: still is 0, a quick move is 1.");
    case F::Curve: return make("Shape", "a drawn curve", "A curve from 0..1 to 0..1.");
    default: break;
    }
    return make("Shape", "", "");
}
