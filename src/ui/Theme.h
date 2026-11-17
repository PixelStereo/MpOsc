#pragma once

#include <QApplication>
#include <QColor>
#include <QIcon>

// The interface is always dark, in the tones of NyxMap.
void applyInterfacePalette(QApplication &app);
QIcon signalPinIcon(bool pinned, const QColor &color);
