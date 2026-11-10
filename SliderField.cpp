#include "SliderField.h"
#include <QDoubleSpinBox>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kFieldHeight = 24;
const QColor kTrack("#1e1e21"), kAccent("#8394b3"), kBorder("#4b4b51");
const QColor kTick(255, 255, 255, 38);

// Flat field at the end of a bar: the value, typed, with its unit and no spin buttons.
QDoubleSpinBox *valueField(QWidget *parent, Qt::Alignment align = Qt::AlignRight)
{
    auto *s = new QDoubleSpinBox(parent);
    s->setButtonSymbols(QAbstractSpinBox::NoButtons);
    s->setAlignment(align | Qt::AlignVCenter);
    s->setKeyboardTracking(false);
    s->setFrame(true);
    s->setFixedHeight(kFieldHeight - 2);
    return s;
}

} // namespace

SliderField::SliderField(QWidget *parent) : QWidget(parent)
{
    setFixedHeight(kFieldHeight);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::SizeHorCursor);
    m_spin = valueField(this);
    m_spin->setRange(0, 1);
    m_spin->hide();
    connect(m_spin, &QDoubleSpinBox::editingFinished, this, [this] {
        m_spin->hide();
        update();
    });
    setToolTip("Drag to adjust. Hold Shift for fine adjustment or double-click to type.");
    connect(m_spin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (std::abs(v - m_value) < 1e-12) return;
        m_value = v;
        update();
        emit valueEdited(v);
        emit editingFinished(v);
    });
}

void SliderField::setRange(double min, double max)
{
    m_min = min;
    m_max = std::max(max, min + 1e-9);
    m_spin->setRange(min, max);
    m_origin = std::clamp(m_origin, m_min, m_max);
    resizeEvent(nullptr);
    update();
}
void SliderField::setTypedRange(double min, double max)
{
    m_spin->setRange(min, max);
    resizeEvent(nullptr);
}
void SliderField::setDecimals(int d) { m_spin->setDecimals(d); resizeEvent(nullptr); }
void SliderField::setSpecialValueText(const QString &text) { m_spin->setSpecialValueText(text); resizeEvent(nullptr); }
void SliderField::setSuffix(const QString &s) { m_spin->setSuffix(s); resizeEvent(nullptr); }
void SliderField::setSingleStep(double s) { m_spin->setSingleStep(s); }
void SliderField::setTicks(int n) { m_ticks = n; update(); }
void SliderField::setSnaps(const std::vector<double> &v) { m_snaps = v; }
void SliderField::setGradient(const QGradientStops &stops) { m_gradient = stops; update(); }
void SliderField::setOrigin(double v) { m_origin = std::clamp(v, m_min, m_max); update(); }

void SliderField::setValue(double v)
{
    v = std::clamp(v, m_spin->minimum(), m_spin->maximum());
    if (std::abs(v - m_value) < 1e-12) return;
    m_value = v;
    QSignalBlocker b(m_spin);
    m_spin->setValue(v);
    update();
}

void SliderField::resizeEvent(QResizeEvent *)
{
    m_spin->setGeometry(rect().adjusted(1, 1, -1, -1));
    update();
}

QRectF SliderField::bar() const { return QRectF(0, 1, width(), height() - 2); }

void SliderField::setCompact(bool on) { setFixedHeight(on ? kFieldHeight - 4 : kFieldHeight); }
double SliderField::valueAt(double x) const
{
    const QRectF r = bar();
    return m_min + std::clamp((x - r.left()) / std::max(1.0, r.width()), 0.0, 1.0) * (m_max - m_min);
}

void SliderField::apply(double v, bool finished)
{
    v = std::clamp(v, m_spin->minimum(), m_spin->maximum());
    const double q = std::pow(10, m_spin->decimals());
    v = std::round(v * q) / q;
    if (std::abs(v - m_value) > 1e-12) {
        m_value = v;
        QSignalBlocker b(m_spin);
        m_spin->setValue(v);
        update();
        emit valueEdited(v);
    }
    if (finished) emit editingFinished(m_value);
}

void SliderField::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = bar();
    p.setPen(Qt::NoPen);
    p.setBrush(kTrack);
    p.drawRoundedRect(r, 4, 4);
    auto xOf = [&](double v) {
        return r.left() + (std::clamp(v, m_min, m_max) - m_min) / (m_max - m_min) * r.width();
    };
    const double x0 = xOf(m_origin), xv = xOf(m_value);
    if (!m_gradient.isEmpty()) {
        // Color bar: the whole track shows the scale, and the value is a marker on it
        QLinearGradient grad(r.topLeft(), r.topRight());
        grad.setStops(m_gradient);
        p.setBrush(grad);
        p.setOpacity(isEnabled() ? 1.0 : 0.4);
        p.drawRoundedRect(r, 4, 4);
        p.setOpacity(1.0);
    } else {
        p.setBrush(isEnabled() ? kAccent : kBorder);
        p.drawRoundedRect(QRectF(QPointF(std::min(x0, xv), r.top()), QPointF(std::max(x0, xv), r.bottom())), 4, 4);
    }
    if (m_ticks > 1) {
        p.setPen(QPen(kTick, 1));
        for (int i = 1; i < m_ticks; ++i) {
            const double x = r.left() + r.width() * i / m_ticks;
            p.drawLine(QPointF(x, r.top() + 3), QPointF(x, r.bottom() - 3));
        }
    }
    if (!m_gradient.isEmpty()) {
        p.setPen(QPen(QColor(20, 20, 22), 3));
        p.drawLine(QPointF(xv, r.top() + 1), QPointF(xv, r.bottom() - 1));
        p.setPen(QPen(Qt::white, 1.4));
        p.drawLine(QPointF(xv, r.top() + 1), QPointF(xv, r.bottom() - 1));
    }
    p.setPen(QPen(kBorder, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    if (hasFocus()) {
        QColor accent = kAccent;
        accent.setAlpha(140);
        p.setPen(QPen(accent, 1));
        p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    }
    p.setPen(QColor(isEnabled() ? "#ffffff" : "#99999f"));
    p.drawText(r.adjusted(5, 0, -7, 0), Qt::AlignRight | Qt::AlignVCenter, m_spin->text());

}

void SliderField::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || !isEnabled() || e->position().x() > bar().right()) return;
    m_drag = true;
    setFocus(Qt::MouseFocusReason);
    m_numberDragging = e->position().x() >= bar().right() - fontMetrics().horizontalAdvance(m_spin->text()) - 12;
    m_numberX = e->position().x();
    m_numberValue = m_value;
    if (!m_numberDragging) apply(valueAt(m_numberX), false);
}
void SliderField::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_drag) return;
    if (m_numberDragging) {
        const double x = e->position().x();
        const double scale = (m_max - m_min) / std::max(1.0, bar().width());
        const double fine = e->modifiers().testFlag(Qt::ShiftModifier) ? 0.1 : 1.0;
        m_numberValue = std::clamp(m_numberValue + (x - m_numberX) * scale * fine, m_spin->minimum(), m_spin->maximum());
        m_numberX = x;
        apply(m_numberValue, false);
        return;
    }
    const double v = valueAt(e->position().x());
    // Shift: ten times finer, from where the value is
    if (e->modifiers() & Qt::ShiftModifier) return apply(m_value + (v - m_value) * 0.1, false);
    // Otherwise the notable values catch the cursor (magnetism), so they can be hit on a wide range
    const double tol = (m_max - m_min) * 0.012;
    double best = v;
    for (double snap : m_snaps)
        if (snap >= m_min && snap <= m_max && std::abs(v - snap) < tol) best = snap;
    apply(best, false);
}
void SliderField::mouseReleaseEvent(QMouseEvent *e)
{
    if (!m_drag || e->button() != Qt::LeftButton) return;
    m_drag = false;
    emit editingFinished(m_value);
}

void SliderField::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;
    m_drag = false;
    m_spin->show();
    m_spin->setFocus(Qt::MouseFocusReason);
    m_spin->selectAll();
}
