#include "SignalFilters.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
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
    const double out = processor.step(it, dt).value_or(raw);
    if (trace) {
        if (prefixes.size() != it.filters.size()) rebuildTrace();
        for (size_t k = 0; k < prefixes.size(); ++k) {
            Prefix &p = *prefixes[k];
            p.processor.input(p.it, raw);
            stages[k] = p.processor.step(p.it, dt).value_or(k ? stages[k - 1] : raw);
        }
    }
    return out;
}

void SignalFilter::reset()
{
    processor.reset();
    for (auto &p : prefixes) p->processor.reset();
}

void SignalFilter::setTraced(bool on)
{
    if (on == trace) return;
    trace = on;
    if (on) rebuildTrace();
    else {
        prefixes.clear();
        stages.clear();
    }
}

void SignalFilter::rebuildTrace()
{
    prefixes.clear();
    for (size_t k = 0; k < it.filters.size(); ++k) {
        auto p = std::make_shared<Prefix>();
        p->it = it;
        p->it.filters.assign(it.filters.begin(), it.filters.begin() + qsizetype(k) + 1);
        prefixes.push_back(std::move(p));
    }
    stages.assign(it.filters.size(), 0.0);
}

QString signalText(SignalType type, bool angle, double value)
{
    if (type == SignalType::Bool) return value >= 0.5 ? QStringLiteral("on") : QStringLiteral("off");
    if (type == SignalType::Int) return QString::number(std::lround(value));
    return angle ? QString::number(value, 'f', 1) + QStringLiteral("°") : QString::number(value, 'f', 3);
}

int signalLevel(SignalType type, bool angle, int minimum, int maximum, double value)
{
    return int(std::lround(type == SignalType::Bool ? (value >= 0.5 ? 1000 : 0)
                           : type == SignalType::Int ? std::clamp(value, 0.0, 2.0)
                           : angle ? std::clamp(value, double(minimum), double(maximum))
                                   : 1000 * std::clamp(value, 0.0, 1.0)));
}

pxs::filterview::Signal filterSignal(const QString &address)
{
    pxs::filterview::Signal signal;
    const auto type = signalType(address);
    const bool angle = isAngle(address);
    signal.title = address;
    switch (type) {
    case SignalType::Bool: signal.tooltip = QStringLiteral("Switch · sent as T / F"); break;
    case SignalType::Int: signal.tooltip = QStringLiteral("Integer · sent as i"); break;
    case SignalType::Float: signal.tooltip = QStringLiteral("Float · sent as f"); break;
    }
    if (angle) {
        const double limit = address.endsWith(QLatin1String("/pitch")) ? 90 : 180;
        signal.lo = -limit;
        signal.hi = limit;
    } else {
        signal.hi = type == SignalType::Int ? 2 : 1;
    }
    signal.sent = [type](double value) {
        if (type == SignalType::Bool) return value >= 0.5 ? 1.0 : 0.0;
        if (type == SignalType::Int) return std::round(value);
        return value;
    };
    signal.text = [type, angle](double value) { return signalText(type, angle, value); };
    signal.offered = filtersOffered(address);
    signal.invert = invertChain(address);
    return signal;
}
