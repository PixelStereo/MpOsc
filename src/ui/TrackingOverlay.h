#pragma once

#include <QJsonObject>
#include <QRectF>

class QPainter;

struct TrackingOverlayOptions {
    enum HandSide { Both, Left, Right };
    HandSide hands = Both;
    int confidence = 0;
    int poseVisibility = 45;
    int strokeScale = 100;
    int pointScale = 100;
    bool handLabels = true;
};

void drawTrackingOverlay(QPainter &painter, const QRectF &area, const QJsonObject &result, bool mirror,
                         const TrackingOverlayOptions &options);
