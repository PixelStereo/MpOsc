#include "HoverHints.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>

HoverHints *HoverHints::instance = nullptr;

void HoverHints::install(QLabel *target)
{
    if (instance) qApp->removeEventFilter(instance);
    delete instance;
    instance = new HoverHints(target);
    qApp->installEventFilter(instance);
}

void HoverHints::show(const QString &text)
{
    if (!instance || !instance->label) return;
    QLabel *label = instance->label;
    const QFontMetrics metrics(label->font());
    label->setText(metrics.elidedText(text, Qt::ElideRight, std::max(label->width(), 200)));
}

void HoverHints::clear()
{
    if (instance && instance->label) instance->label->clear();
}

void HoverHints::set(QObject *who, const QString &text)
{
    if (text.isEmpty()) {
        if (owner == who) {
            owner = nullptr;
            clear();
        }
        return;
    }
    owner = who;
    show(text);
}

void HoverHints::watch(QMenu *menu)
{
    QObject::connect(menu, &QMenu::hovered, menu, [](QAction *action) {
        QString text = action->toolTip();
        if (text == action->text().remove(QLatin1Char('&'))) text.clear(); // the tool tip a menu entry has by default is its text
        if (text.isEmpty()) clear();
        else show(text);
    });
    QObject::connect(menu, &QMenu::aboutToHide, menu, [] { clear(); });
}

bool HoverHints::eventFilter(QObject *watched, QEvent *event)
{
    auto *widget = qobject_cast<QWidget *>(watched);
    if (!widget) return false;
    switch (event->type()) {
    case QEvent::ToolTip:
        return true; // said in the status bar instead
    case QEvent::Enter:
        if (!qobject_cast<QMenu *>(widget)) set(widget, widget->toolTip());
        break;
    case QEvent::Leave:
        if (owner == widget) set(widget, QString());
        break;
    case QEvent::MouseMove:
        if (auto *view = qobject_cast<QAbstractItemView *>(widget->parentWidget()); view && view->viewport() == widget) {
            const auto position = static_cast<QMouseEvent *>(event)->position().toPoint();
            set(widget, view->indexAt(position).data(Qt::ToolTipRole).toString());
        }
        break;
    default: break;
    }
    return false;
}
