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
