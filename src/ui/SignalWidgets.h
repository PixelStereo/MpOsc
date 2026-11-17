#pragma once

#include "Theme.h"

#include <QApplication>
#include <QEvent>
#include <QToolButton>
#include <QTreeWidgetItem>

class PinButton final : public QToolButton {
public:
    PinButton()
    {
        setCheckable(true);
        setFixedSize(24, 24);
        setIconSize(QSize(16, 16));
        setCursor(Qt::PointingHandCursor);
        setObjectName("signalPin");
        connect(this, &QToolButton::toggled, this, [this] { refreshIcon(); });
        refreshIcon();
    }

protected:
    void changeEvent(QEvent *event) override
    {
        QToolButton::changeEvent(event);
        if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) refreshIcon();
    }

private:
    void refreshIcon() { setIcon(signalPinIcon(isChecked(), QApplication::palette().color(QPalette::Link))); }
};

class SignalItem final : public QTreeWidgetItem
{
public:
    SignalItem(QTreeWidget *tree, const QString &address) : QTreeWidgetItem(tree)
    {
        setData(0, Qt::UserRole, address);
        setData(0, Qt::AccessibleTextRole, address);
        setSizeHint(0, QSize(0, 26));
    }

    bool operator<(const QTreeWidgetItem &other) const override
    {
        return data(0, Qt::UserRole).toString() < other.data(0, Qt::UserRole).toString();
    }
};
