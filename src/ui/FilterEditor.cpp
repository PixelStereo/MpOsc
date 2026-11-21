#include "FilterEditor.h"

#include "HoverHints.h"
#include "SignalMeter.h"
#include "SliderField.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

void addFilterActions(QMenu *menu, const std::vector<InteractionFilter> &kinds, const std::function<void(InteractionFilter)> &chosen)
{
    HoverHints::watch(menu); // what a filter is, in the status bar as the pointer goes over it
    for (const auto &group : filterGroups()) {
        QMenu *sub = nullptr;
        for (auto kind : kinds) {
            const auto info = filterInfo(kind);
            if (info.group != group) continue;
            if (!sub) {
                sub = menu->addMenu(group);
                HoverHints::watch(sub);
            }
            auto *action = sub->addAction(interactionFilterName(kind));
            action->setToolTip(QStringLiteral("%1. %2").arg(info.summary, info.detail));
            QObject::connect(action, &QAction::triggered, menu, [chosen, kind] { chosen(kind); });
        }
    }
}

namespace {
QString typeText(SignalType type)
{
    switch (type) {
    case SignalType::Bool: return QStringLiteral("Switch · sent as T / F");
    case SignalType::Int: return QStringLiteral("Integer · sent as i");
    case SignalType::Float: return QStringLiteral("Float · sent as f");
    }
    return {};
}

// A bar like the signal table's: the same range and the same text for the same signal
SignalMeter *makeMeter(const QString &address)
{
    auto *meter = new SignalMeter;
    const auto type = signalType(address);
    if (isAngle(address)) {
        const int limit = address.endsWith("/pitch") ? 90 : 180;
        meter->setRange(-limit, limit);
    } else {
        meter->setRange(0, type == SignalType::Int ? 2 : 1000);
    }
    meter->setObjectName("signalValue");
    meter->setFixedHeight(16);
    meter->setFormat(QStringLiteral("—"));
    meter->setValue(meter->minimum());
    return meter;
}

// What a card is made of: a fold button, its on / off box, its name, a remove button, the signal after it, then its settings
class FilterCard : public QWidget {
public:
    QToolButton *fold, *remove;
    QCheckBox *enabled;
    QLabel *name;
    SignalMeter *meter;
    QWidget *body;
    bool unfolded;

    // The height it takes: added up by hand, the layouts of a card that is not shown yet answer too small
    int height()
    {
        auto *outer = static_cast<QVBoxLayout *>(layout());
        auto *inner = static_cast<QVBoxLayout *>(body->layout());
        int total = outer->contentsMargins().top() + outer->contentsMargins().bottom() + outer->itemAt(0)->sizeHint().height() +
                    outer->spacing() + meter->height();
        if (unfolded && inner->count() > 0) total += outer->spacing() + int(inner->count()) * 26 + (int(inner->count()) - 1) * inner->spacing();
        return total;
    }

    FilterCard(const QString &address, const InteractionStage &stage, bool open) : unfolded(open)
    {
        auto *column = new QVBoxLayout(this);
        column->setContentsMargins(6, 4, 6, 4);
        column->setSpacing(4);
        auto *header = new QHBoxLayout;
        fold = new QToolButton;
        fold->setAutoRaise(true);
        fold->setArrowType(unfolded ? Qt::DownArrow : Qt::RightArrow);
        fold->setToolTip("Show or hide the settings");
        enabled = new QCheckBox;
        enabled->setChecked(stage.enabled);
        enabled->setToolTip("This filter on or off");
        enabled->setAccessibleName("Filter on");
        name = new QLabel(stage.displayName());
        name->setAttribute(Qt::WA_TransparentForMouseEvents); // a drag from the name moves the card
        remove = new QToolButton;
        remove->setText(QStringLiteral("×"));
        remove->setAutoRaise(true);
        remove->setToolTip("Remove this filter");
        remove->setAccessibleName("Remove filter");
        header->addWidget(fold);
        header->addWidget(enabled);
        header->addWidget(name, 1);
        header->addWidget(remove);
        column->addLayout(header);
        meter = makeMeter(address);
        meter->setToolTip(QStringLiteral("The signal after %1").arg(stage.displayName()));
        column->addWidget(meter);
        body = new QWidget;
        new QVBoxLayout(body);
        body->layout()->setContentsMargins(24, 0, 0, 0);
        body->layout()->setSpacing(4);
        column->addWidget(body);
        body->setVisible(unfolded);
        // What the filter does is said in the status bar, as the pointer goes over its card
        const auto info = filterInfo(stage.kind);
        setToolTip(QStringLiteral("%1. %2").arg(info.summary, info.detail));
    }
};
}

FilterEditor::FilterEditor(QWidget *parent) : QWidget(parent)
{
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(6, 0, 0, 0);
    title = new QLabel;
    QFont bold = title->font();
    bold.setBold(true);
    title->setFont(bold);
    title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    column->addWidget(title);
    auto *tools = new QHBoxLayout;
    add = new QPushButton("Add filter");
    clear = new QToolButton;
    clear->setText("Clear");
    clear->setToolTip("Remove every filter of this signal");
    tools->addWidget(add);
    tools->addWidget(clear);
    tools->addStretch();
    column->addLayout(tools);
    input = new QProgressBar;
    input->setObjectName("signalValue");
    input->setFixedHeight(16);
    input->setToolTip("The signal as it comes in, before the filters");
    column->addWidget(input);
    list = new QListWidget;
    list->setDragDropMode(QAbstractItemView::InternalMove);
    list->setDefaultDropAction(Qt::MoveAction);
    list->setSelectionMode(QAbstractItemView::NoSelection);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setSpacing(3);
    column->addWidget(list, 1);
    connect(clear, &QToolButton::clicked, this, [this] {
        chain.clear();
        open.clear();
        rebuild();
        edited(true);
    });
    // A card dropped elsewhere: the chain follows the new order of the list (rebuilt once the drop is over)
    connect(list->model(), &QAbstractItemModel::rowsMoved, this, [this] {
        if (rebuilding) return;
        FilterChain moved;
        std::vector<bool> unfolded;
        for (int row = 0; row < list->count(); ++row) {
            const auto index = list->item(row)->data(Qt::UserRole).toInt();
            moved.push_back(chain[size_t(index)]);
            unfolded.push_back(open[size_t(index)]);
        }
        chain = moved;
        open = unfolded;
        QTimer::singleShot(0, this, [this] {
            rebuild();
            edited(true);
        });
    });
    setSignal({}, {});
}

void FilterEditor::setSignal(const QString &address, const FilterChain &filters)
{
    signalAddress = address;
    chain = filters;
    open.assign(chain.size(), false);
    const bool has = !address.isEmpty();
    kind = has ? signalType(address) : SignalType::Float;
    angle = has && isAngle(address);
    title->setText(has ? address : QString());
    title->setToolTip(has ? typeText(kind) : QString());
    add->setEnabled(has);
    clear->setEnabled(has);
    list->setVisible(has);
    input->setVisible(has);
    if (has) {
        if (angle) input->setRange(address.endsWith("/pitch") ? -90 : -180, address.endsWith("/pitch") ? 90 : 180);
        else input->setRange(0, kind == SignalType::Int ? 2 : 1000);
        input->setFormat(QStringLiteral("—"));
        input->setValue(input->minimum());
        auto *menu = new QMenu(add);
        HoverHints::watch(menu);
        auto *invert = menu->addAction("Invert", this, [this] {
            for (auto &stage : invertChain(signalAddress)) {
                chain.push_back(stage);
                open.push_back(false);
            }
            rebuild();
            edited(true);
        });
        invert->setToolTip("Flips the signal (1 − x, on ↔ off, −x for an angle)");
        menu->addSeparator();
        addFilterActions(menu, filtersOffered(address), [this](InteractionFilter filter) { addFilter(filter); });
        delete add->menu();
        add->setMenu(menu);
    }
    rebuild();
}

void FilterEditor::setTrace(bool available, double value, const std::vector<double> &afterEach)
{
    const auto show = [&](QProgressBar *bar, double v) {
        if (!available) {
            bar->setFormat(QStringLiteral("—"));
            bar->setValue(bar->minimum());
            return;
        }
        bar->setFormat(signalText(kind, angle, signalValue(kind, v).toDouble()));
        bar->setValue(signalLevel(kind, angle, bar->minimum(), bar->maximum(), v));
    };
    show(input, value);
    for (size_t k = 0; k < meters.size(); ++k) {
        show(meters[k], k < afterEach.size() ? afterEach[k] : value);
        // The signal as it came in, marked on each bar: how far each filter has taken it
        meters[k]->setReference(available, signalLevel(kind, angle, meters[k]->minimum(), meters[k]->maximum(), value));
    }
}

void FilterEditor::addFilter(InteractionFilter kind)
{
    chain.push_back(InteractionStage::made(kind));
    open.push_back(true);
    rebuild();
    edited(true);
}

void FilterEditor::edited(bool final)
{
    emit chainChanged(signalAddress, chain, final);
}

void FilterEditor::fitItems()
{
    for (int row = 0; row < list->count(); ++row) {
        auto *item = list->item(row);
        auto *card = dynamic_cast<FilterCard *>(list->itemWidget(item));
        if (!card) continue;
        const int width = std::max(120, list->viewport()->width() - 2 * list->spacing());
        item->setSizeHint(QSize(width, card->height()));
    }
}

void FilterEditor::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    fitItems();
}

void FilterEditor::rebuild()
{
    rebuilding = true;
    meters.clear();
    list->clear();
    for (size_t i = 0; i < chain.size(); ++i) addCard(int(i));
    fitItems();
    rebuilding = false;
}

void FilterEditor::addCard(int index)
{
    auto *item = new QListWidgetItem(list);
    item->setData(Qt::UserRole, index);
    auto *card = new FilterCard(signalAddress, chain[size_t(index)], open[size_t(index)]);
    meters.push_back(card->meter);
    card->setObjectName("filterCard");
    card->setAttribute(Qt::WA_StyledBackground);
    card->setStyleSheet("QWidget#filterCard { border:1px solid palette(mid); border-radius:5px; }");
    list->setItemWidget(item, card);
    connect(card->fold, &QToolButton::clicked, this, [this, card, index] {
        const bool unfolded = !card->unfolded;
        card->unfolded = unfolded;
        open[size_t(index)] = unfolded;
        card->body->setVisible(unfolded);
        card->fold->setArrowType(unfolded ? Qt::DownArrow : Qt::RightArrow);
        card->layout()->activate();
        fitItems();
    });
    connect(card->enabled, &QCheckBox::toggled, this, [this, index](bool on) {
        chain[size_t(index)].enabled = on;
        edited(true);
    });
    connect(card->remove, &QToolButton::clicked, this, [this, index] {
        QTimer::singleShot(0, this, [this, index] {
            chain.erase(chain.begin() + index);
            open.erase(open.begin() + index);
            rebuild();
            edited(true);
        });
    });
    auto &stage = chain[size_t(index)];
    auto *body = static_cast<QVBoxLayout *>(card->body->layout());
    const auto addRow = [&](const QString &label, QWidget *field) {
        auto *row = new QHBoxLayout;
        auto *text = new QLabel(label);
        text->setMinimumWidth(64);
        row->addWidget(text);
        row->addWidget(field, 1);
        body->addLayout(row);
    };
    if (stage.kind == InteractionFilter::Math) {
        auto *operation = new QComboBox;
        for (int op = 0; op < kMathOpCount; ++op) operation->addItem(mathOpName(MathOp(op)), op);
        operation->setCurrentIndex(int(stage.op));
        addRow("Operation", operation);
        connect(operation, &QComboBox::activated, this, [this, index, operation](int) {
            chain[size_t(index)].op = MathOp(operation->currentData().toInt());
            edited(true);
            QTimer::singleShot(0, this, [this] { rebuild(); }); // its value is there only for the operations that use one
        });
    }
    for (int k = 0; k < kFilterSettingCount; ++k) {
        const auto setting = interactionFilterSetting(stage.kind, k);
        if (setting.name.isEmpty()) continue;
        if (stage.kind == InteractionFilter::Math && !mathOpUsesValue(stage.op)) continue;
        auto *field = new SliderField;
        field->setDecimals(3);
        field->setRange(setting.min, setting.max);
        if (setting.suffix.trimmed() == "s") field->setTypedRange(setting.min, std::max(setting.max, 600.0));
        field->setSingleStep((setting.max - setting.min) / 100);
        field->setSuffix(setting.suffix);
        field->setValue(stage.setting(k));
        addRow(setting.name, field);
        connect(field, &SliderField::valueEdited, this, [this, index, k](double v) {
            chain[size_t(index)].setting(k) = v;
            edited(false);
        });
        connect(field, &SliderField::editingFinished, this, [this, index, k](double v) {
            chain[size_t(index)].setting(k) = v;
            edited(true);
        });
    }
    card->layout()->activate();
}
