#pragma once

#include "TrackingOverlay.h"

#include <QImage>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWidget>
#include <algorithm>

class VideoPreview final : public QWidget
{
public:
    using QWidget::QWidget;
    QRectF roi{0, 0, 1, 1};
    TrackingOverlayOptions overlayOptions;
    bool editRoi = false, mirror = false, showLandmarks = true;
    bool isDrawing() const { return dragging; }
    void showFrame(const QImage &frame, const QJsonObject &tracking = {}, const QRectF &trackedArea = {})
    {
        image = frame;
        result = tracking;
        inferenceRegion = trackedArea;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#161618"));
        if (image.isNull()) {
            painter.setPen(QColor("#e1e1e4"));
            painter.drawText(rect(), Qt::AlignCenter, "Choose a video input to begin");
            return;
        }
        const auto area = imageRect();
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.save();
        if (mirror) {
            painter.translate(2 * area.center().x(), 0);
            painter.scale(-1, 1);
        }
        painter.drawImage(area, image);
        painter.restore();

        if (showLandmarks && !inferenceRegion.isEmpty())
            drawTrackingOverlay(painter, toPreview(inferenceRegion, area), result, mirror, overlayOptions);
        if (editRoi || roi != QRectF(0, 0, 1, 1)) {
            const auto selection = toPreview(roi, area);
            QPainterPath shade;
            shade.addRect(area);
            shade.addRect(selection);
            painter.fillPath(shade, QColor(0, 0, 0, 214));
            painter.setPen(QPen(QColor("#d4d4d8"), 1.2, editRoi ? Qt::DashLine : Qt::SolidLine));
            painter.drawRect(selection.adjusted(1, 1, -1, -1));
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor("#d4d4d8"));
            for (const auto &corner : {selection.topLeft(), selection.topRight(), selection.bottomLeft(), selection.bottomRight()})
                painter.drawEllipse(corner, 2.5, 2.5);
        }

    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (!editRoi || event->button() != Qt::LeftButton || !imageRect().contains(event->position())) return;
        anchor = imagePoint(event->position());
        dragging = true;
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (dragging) select(event->position());
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (!dragging || event->button() != Qt::LeftButton) return;
        select(event->position());
        dragging = false;
    }

private:
    QRectF toPreview(const QRectF &region, const QRectF &area) const
    {
        const auto x = mirror ? 1 - region.right() : region.x();
        return QRectF(area.x() + x * area.width(), area.y() + region.y() * area.height(),
                      region.width() * area.width(), region.height() * area.height());
    }

    QRectF imageRect() const
    {
        if (image.isNull()) return {};
        const QSize size = image.size().scaled(this->size(), Qt::KeepAspectRatio);
        return QRectF(QPointF((width() - size.width()) / 2., (height() - size.height()) / 2.), size);
    }
    QPointF imagePoint(const QPointF &point) const
    {
        const auto area = imageRect();
        const auto x = std::clamp((point.x() - area.x()) / std::max(1., area.width()), 0., 1.);
        return {mirror ? 1 - x : x,
                std::clamp((point.y() - area.y()) / std::max(1., area.height()), 0., 1.)};
    }
    void select(const QPointF &point)
    {
        const auto selection = QRectF(anchor, imagePoint(point)).normalized();
        if (selection.width() * image.width() >= 2 && selection.height() * image.height() >= 2) {
            roi = selection;
            update();
        }
    }
    QImage image;
    QJsonObject result;
    QRectF inferenceRegion;
    QPointF anchor;
    bool dragging = false;
};
