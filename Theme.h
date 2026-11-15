#pragma once

#include <QApplication>
#include <QColor>
#include <QIcon>

void applyInterfacePalette(QApplication &app, Qt::ColorScheme scheme);
QIcon signalPinIcon(bool pinned, const QColor &color);
