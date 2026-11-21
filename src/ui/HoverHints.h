#pragma once

#include <QObject>
#include <QPointer>

class QLabel;
class QMenu;
class QWidget;

// What a control is for is said in the status bar, at its right, as the pointer goes over it: the tool tip of a widget (or
// of an item of a list or a table, or of the entry of a menu) goes there and the pop-up is not shown. Written as tool tips
// everywhere, read in one place.
class HoverHints final : public QObject {
public:
    // Every tool tip of the application is shown in `target` from now on
    static void install(QLabel *target);
    // The entries of a menu say what they do while the pointer is over them
    static void watch(QMenu *menu);
    static void show(const QString &text);
    static void clear();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    explicit HoverHints(QLabel *target) : label(target) {}
    void set(QObject *owner, const QString &text);

    QPointer<QLabel> label;
    QPointer<QObject> owner;
    static HoverHints *instance;
};
