#pragma once
// SliderField adapted from NyxMap by Pixel Stereo (GPL-3.0).
#include <QWidget>
#include <QGradient>
#include <vector>

class SliderField : public QWidget
{
    Q_OBJECT
public:
    explicit SliderField(QWidget *parent = nullptr);
    void setRange(double min, double max);
    // Widens what the field accepts beyond the bar: the bar covers the useful range, extreme values are typed
    void setTypedRange(double min, double max);
    void setDecimals(int d);
    void setSpecialValueText(const QString &text);
    void setSuffix(const QString &s);  // " %", " s", " ×"
    void setSingleStep(double s);
    void setTicks(int n);              // tick marks drawn on the bar (0: none)
    void setSnaps(const std::vector<double> &v); // values the cursor catches while dragging (0, 100 %…)
    // Paints the bar with this gradient instead of a fill: the value is then shown as a marker on it
    void setGradient(const QGradientStops &stops);
    void setOrigin(double v);          // where the fill starts (0 by default; 0 in the middle for a ± range)
    void setValue(double v);           // no signal
    void setCompact(bool on);          // a lower bar (the channels of a color)
    double value() const { return m_value; }
    bool isDragging() const { return m_drag; }
    QSize sizeHint() const override { return QSize(220, 20); }

signals:
    void valueEdited(double v);   // continuous while dragging, and on a typed value
    void editingFinished(double v);

protected:
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    QRectF bar() const;
    double valueAt(double x) const;
    void apply(double v, bool finished);
    class QDoubleSpinBox *m_spin;
    double m_value = 0, m_min = 0, m_max = 1, m_origin = 0;
    int m_ticks = 0;
    bool m_drag = false;
    bool m_numberDragging = false;
    double m_numberX = 0, m_numberValue = 0;
    std::vector<double> m_snaps;
    QGradientStops m_gradient;
};
