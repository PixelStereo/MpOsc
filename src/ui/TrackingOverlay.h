#pragma once

#include <QJsonObject>
#include <QRectF>

class QPainter;

void drawTrackingOverlay(QPainter &painter, const QRectF &area, const QJsonObject &result, bool mirror);
