#include "FilterEditor.h"

#include "SliderField.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

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

// What a card is made of: a fold button, its on / off box, its name, a remove button, then its settings
class FilterCard : public QWidget {
public:
    QToolButton *fold, *remove;
    QCheckBox *enabled;
    QLabel *name;
    QWidget *body;

    FilterCard(const InteractionStage &stage, bool unfolded)
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
        body = new QWidget;
        new QVBoxLayout(body);
        body->layout()->setContentsMargins(24, 0, 0, 0);
        body->layout()->setSpacing(4);
        body->setVisible(unfolded);
        column->addWidget(body);
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
    type = new QLabel;
    live = new QLabel;
    column->addWidget(title);
    column->addWidget(type);
    column->addWidget(live);
    auto *tools = new QHBoxLayout;
    add = new QPushButton("Add filter");
    clear = new QToolButton;
    clear->setText("Clear");
    clear->setToolTip("Remove every filter of this signal");
    tools->addWidget(add);
    tools->addWidget(clear);
    tools->addStretch();
    column->addLayout(tools);
    list = new QListWidget;
    list->setDragDropMode(QAbstractItemView::InternalMove);
    list->setDefaultDropAction(Qt::MoveAction);
    list->setSelectionMode(QAbstractItemView::NoSelection);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setSpacing(3);
    column->addWidget(list, 1);
    empty = new QLabel;
    empty->setWordWrap(true);
    empty->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    column->addWidget(empty, 1);
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
    title->setText(has ? address : QString());
    type->setText(has ? typeText(signalType(address)) : QString());
    live->clear();
    add->setEnabled(has);
    clear->setEnabled(has);
    list->setVisible(has);
    empty->setVisible(!has);
    empty->setText("Select a signal to filter it: its value goes through the filters you add, one after the other, "
                   "before it is sent and shown.\n\nRight-click a family (face, left hand, right hand, body) to give "
                   "a filter to all of its signals.");
    if (has) {
        auto *menu = new QMenu(add);
        menu->addAction("Invert", this, [this] {
            for (auto &stage : invertChain(signalAddress)) {
                chain.push_back(stage);
                open.push_back(false);
            }
            rebuild();
            edited(true);
        });
        menu->addSeparator();
        for (auto kind : filtersOffered(address)) menu->addAction(interactionFilterName(kind), this, [this, kind] { addFilter(kind); });
        delete add->menu();
        add->setMenu(menu);
    }
    rebuild();
}

void FilterEditor::setLive(const QString &input, const QString &output)
{
    live->setText(signalAddress.isEmpty() ? QString() : QStringLiteral("%1  →  %2").arg(input, output));
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
        if (auto *widget = list->itemWidget(item)) item->setSizeHint(widget->sizeHint());
    }
}

void FilterEditor::rebuild()
{
    rebuilding = true;
    list->clear();
    for (size_t i = 0; i < chain.size(); ++i) addCard(int(i));
    fitItems();
    rebuilding = false;
}

void FilterEditor::addCard(int index)
{
    auto *item = new QListWidgetItem(list);
    item->setData(Qt::UserRole, index);
    auto *card = new FilterCard(chain[size_t(index)], open[size_t(index)]);
    card->setObjectName("filterCard");
    card->setAttribute(Qt::WA_StyledBackground);
    card->setStyleSheet("QWidget#filterCard { border:1px solid palette(mid); border-radius:5px; }");
    list->setItemWidget(item, card);
    connect(card->fold, &QToolButton::clicked, this, [this, card, item, index] {
        const bool unfolded = !card->body->isVisible();
        open[size_t(index)] = unfolded;
        card->body->setVisible(unfolded);
        card->fold->setArrowType(unfolded ? Qt::DownArrow : Qt::RightArrow);
        card->layout()->activate();
        item->setSizeHint(card->sizeHint());
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
