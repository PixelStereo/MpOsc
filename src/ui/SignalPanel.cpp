#include "SignalPanel.h"

#include "Destinations.h"
#include "FilterEditor.h"
#include "OscCodec.h"
#include "SignalMeter.h"
#include "SignalWidgets.h"
#include "Signals.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {
QString settingsKey(const QString &address) { return QStringLiteral("signals") + address; }

// The chain of another signal, for this one: what its type has no use for is left out, an inversion is this signal's own
FilterChain adapted(const FilterChain &chain, const QString &address)
{
    const auto offered = filtersOffered(address);
    FilterChain result;
    for (const auto &stage : chain) {
        if (stage.kind == InteractionFilter::Math && stage.name.startsWith(QLatin1String("Invert"))) {
            if (stage.name == QLatin1String("Invert")) // the second stage of an integer's inversion is left behind with it
                for (const auto &mine : invertChain(address)) result.push_back(mine);
        } else if (std::find(offered.begin(), offered.end(), stage.kind) != offered.end()) {
            result.push_back(stage);
        }
    }
    return result;
}
}

SignalPanel::SignalPanel(QWidget *parent) : QWidget(parent)
{
    auto *controls = new QVBoxLayout(this);
    search = new QLineEdit;
    search->setPlaceholderText("Filter signals by name");
    auto *signalActions = new QHBoxLayout;
    auto *enableAll = new QPushButton("Enable all");
    auto *disableAll = new QPushButton("Disable all");
    signalActions->addWidget(search, 1);
    signalActions->addWidget(enableAll);
    signalActions->addWidget(disableAll);
    controls->addLayout(signalActions);
    connect(enableAll, &QPushButton::clicked, this, [this] {
        for (auto &row : rows) row.send->setChecked(true);
    });
    connect(disableAll, &QPushButton::clicked, this, [this] {
        for (auto &row : rows) row.send->setChecked(false);
    });
    connect(search, &QLineEdit::textChanged, this, [this](const QString &text) {
        query = text;
        applyQuery();
    });
    const auto makeTable = [this](bool pinned) {
        auto *view = new QTreeWidget;
        view->setRootIsDecorated(!pinned);
        view->setSelectionMode(QAbstractItemView::SingleSelection);
        view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        view->setHeaderLabels({"Signal", "Value", "Send", "Filters"});
        auto *header = view->header();
        header->setStretchLastSection(false);
        header->setSectionResizeMode(0, QHeaderView::Stretch);
        for (int column : {1, 2, 3}) header->setSectionResizeMode(column, QHeaderView::Fixed);
        header->resizeSection(1, 90);
        header->resizeSection(2, 46);
        header->resizeSection(3, 52);
        header->setSortIndicatorShown(false);
        header->setSectionsClickable(false);
        view->setSortingEnabled(true);
        view->sortItems(0, Qt::AscendingOrder);
        view->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(view, &QTreeWidget::itemSelectionChanged, this, [this, view] {
            const auto items = view->selectedItems();
            if (items.isEmpty()) return;
            auto *other = view == table ? pinnedTable : table;
            other->clearSelection();
            select(items.first()->data(0, Qt::UserRole).toString());
        });
        connect(view, &QTreeWidget::itemClicked, this, [view](QTreeWidgetItem *item) {
            if (item->childCount() > 0) item->setExpanded(!item->isExpanded()); // a family folds on a click
        });
        connect(view, &QTreeWidget::customContextMenuRequested, this, [this, view](const QPoint &point) {
            auto *item = view->itemAt(point);
            if (item && !item->parent() && item->childCount() > 0 && view == table)
                familyMenu(item->data(0, Qt::UserRole).toString(), view->viewport()->mapToGlobal(point));
        });
        return view;
    };
    pinnedTable = makeTable(true);
    table = makeTable(false);
    pinnedTable->hide();
    auto *left = new QWidget;
    auto *tables = new QVBoxLayout(left);
    tables->setContentsMargins(0, 0, 0, 0);
    tables->addWidget(pinnedTable);
    tables->addWidget(table, 1);
    editor = new FilterEditor;
    editor->setMinimumWidth(250);
    editor->hide(); // opened with Ctrl+M
    auto *split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    split->addWidget(left);
    split->addWidget(editor);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    controls->addWidget(split, 1);
    connect(editor, &FilterEditor::chainChanged, this, [this](const QString &address, const FilterChain &chain, bool final) {
        setChain(address, chain, final);
    });
    connect(table, &QTreeWidget::itemExpanded, this, [](QTreeWidgetItem *item) {
        QSettings().setValue("window/collapsed" + item->data(0, Qt::UserRole).toString(), false);
    });
    connect(table, &QTreeWidget::itemCollapsed, this, [](QTreeWidgetItem *item) {
        QSettings().setValue("window/collapsed" + item->data(0, Qt::UserRole).toString(), true);
    });
    clock.start();
}

QString SignalPanel::filter() const { return search->text(); }
void SignalPanel::setFilter(const QString &text) { search->setText(text); }

FilterChain SignalPanel::chainOf(const QString &address) const
{
    const auto row = rows.constFind(address);
    return row == rows.cend() ? FilterChain{} : row->filter.chain();
}

void SignalPanel::setSending(bool enabled)
{
    sending = enabled;
    for (auto &row : rows) {
        row.sent = false;
        row.meter->setEnabled(sending && row.send->isChecked());
    }
}

void SignalPanel::resetSent()
{
    for (auto &row : rows) row.sent = false;
}

void SignalPanel::resetValues()
{
    for (auto &row : rows) {
        row.initialized = row.sent = false;
        row.filter.reset();
    }
}

void SignalPanel::clear()
{
    rows.clear();
    families.clear();
    selected.clear();
    table->clear();
    pinnedTable->clear();
    editor->setSignal({}, {});
    resizePinnedTable();
}

void SignalPanel::restoreRows(const QSettings &settings)
{
    for (const auto &key : settings.allKeys()) {
        if (!key.startsWith("signals/")) continue;
        const auto end = key.lastIndexOf('/');
        if (end <= 7) continue;
        const auto address = key.mid(7, end - 7);
        if (!rows.contains(address)) rows.insert(address, makeRow(address));
    }
    applyQuery();
}

void SignalPanel::resizePinnedTable()
{
    const int count = pinnedTable->topLevelItemCount();
    pinnedTable->setVisible(count > 0);
    table->setHeaderHidden(count > 0);
    if (count > 0) {
        const int rowHeight = std::max(26, pinnedTable->visualItemRect(pinnedTable->topLevelItem(0)).height());
        pinnedTable->setFixedHeight(pinnedTable->header()->sizeHint().height() + std::min(count, 6) * rowHeight +
                                    2 * pinnedTable->frameWidth());
    }
}

void SignalPanel::applyQuery()
{
    for (auto i = rows.begin(); i != rows.end(); ++i)
        if (i->item) i->item->setHidden(!i.key().contains(query, Qt::CaseInsensitive));
    for (auto *family : families) {
        bool any = false;
        for (int c = 0; c < family->childCount(); ++c) any = any || !family->child(c)->isHidden();
        family->setHidden(!any);
        if (any && !query.isEmpty()) family->setExpanded(true);
    }
}

FamilyItem *SignalPanel::familyItem(const QString &family)
{
    if (auto *item = families.value(family)) return item;
    auto *item = new FamilyItem(table, family);
    families.insert(family, item);
    item->setText(0, family + "/*");
    item->setExpanded(!QSettings().value("window/collapsed" + family, false).toBool());
    return item;
}

void SignalPanel::removeItem(Row &row)
{
    if (!row.item) return;
    auto *family = row.item->parent();
    delete row.item;
    row.item = nullptr;
    if (family && family->childCount() == 0) {
        families.remove(family->data(0, Qt::UserRole).toString());
        delete family;
    }
}

void SignalPanel::moveSignal(const QString &address, bool pinned)
{
    auto row = rows.find(address);
    if (row == rows.end() || row->pinned == pinned) return;
    removeItem(*row);
    row->pinned = pinned;
    buildItem(address, *row);
    if (selected == address) (pinned ? pinnedTable : table)->setCurrentItem(row->item);
    applyQuery();
    resizePinnedTable();
}

void SignalPanel::setSignalKnown(std::function<void(const QString &)> callback)
{
    signalKnown = std::move(callback);
    if (signalKnown)
        for (auto i = rows.cbegin(); i != rows.cend(); ++i) signalKnown(i.key());
}

void SignalPanel::showBadge(Row &row)
{
    const auto count = row.filter.chain().size();
    row.item->setText(3, count ? QStringLiteral("≋ %1").arg(count) : QString());
    row.item->setTextAlignment(3, Qt::AlignCenter);
    row.item->setToolTip(3, count ? QStringLiteral("%1 filters").arg(count) : QString());
}

// The row of a signal, in the pinned table or under its family
void SignalPanel::buildItem(const QString &address, Row &row)
{
    const auto key = settingsKey(address);
    auto *view = row.pinned ? pinnedTable : table;
    auto *item = row.pinned ? new SignalItem(pinnedTable, address) : new SignalItem(familyItem(signalFamily(address)), address);
    row.item = item;

    auto *nameCell = new QWidget;
    auto *nameLayout = new QHBoxLayout(nameCell);
    nameLayout->setContentsMargins(1, 1, 1, 1);
    nameLayout->setSpacing(4);
    auto *pin = new PinButton;
    pin->setChecked(row.pinned);
    pin->setToolTip(row.pinned ? "Unpin signal" : "Pin signal");
    pin->setAccessibleName(pin->toolTip());
    connect(pin, &QToolButton::toggled, this, [this, address, key](bool enabled) {
        QSettings().setValue(key + "/pinned", enabled);
        moveSignal(address, enabled);
    }, Qt::QueuedConnection);
    auto *name = new QLabel(row.pinned ? address : address.mid(signalFamily(address).size() + 1));
    name->setToolTip(address);
    name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    name->setAttribute(Qt::WA_TransparentForMouseEvents); // a click on the name selects the row
    nameLayout->addWidget(pin, 0, Qt::AlignVCenter);
    nameLayout->addWidget(name, 1);
    view->setItemWidget(item, 0, nameCell);

    auto *meter = new SignalMeter;
    const auto type = signalType(address);
    if (isAngle(address)) {
        const int limit = address.endsWith("/pitch") ? 90 : 180;
        meter->setRange(-limit, limit);
    } else {
        meter->setRange(0, type == SignalType::Int ? 2 : 1000);
    }
    meter->setObjectName("signalValue");
    meter->setFormat(row.shown);
    meter->setValue(row.level);
    view->setItemWidget(item, 1, meter);
    row.meter = meter;

    auto *send = new QCheckBox;
    send->setAccessibleName("Send " + address);
    send->setToolTip("Send this signal to the OSC destinations");
    send->setChecked(QSettings().value(key + "/send", false).toBool());
    auto *sendCell = new QWidget;
    auto *sendLayout = new QHBoxLayout(sendCell);
    sendLayout->setContentsMargins(0, 0, 0, 0);
    sendLayout->addWidget(send, 0, Qt::AlignCenter);
    view->setItemWidget(item, 2, sendCell);
    row.send = send;
    meter->setEnabled(sending && send->isChecked());
    connect(send, &QCheckBox::toggled, this, [this, address, key](bool enabled) {
        auto &r = rows[address];
        r.sent = false;
        r.meter->setEnabled(sending && enabled);
        QSettings().setValue(key + "/send", enabled);
    });
    showBadge(row);
    resizePinnedTable();
}

SignalPanel::Row SignalPanel::makeRow(const QString &address)
{
    if (signalKnown) signalKnown(address);
    const auto key = settingsKey(address);
    QSettings settings;
    Row row;
    row.pinned = settings.value(key + "/pinned", false).toBool();
    row.filter.setChain(chainFromText(settings.value(key + "/filters").toString()));
    buildItem(address, row);
    return row;
}

void SignalPanel::select(const QString &address)
{
    selected = address;
    for (auto &row : rows) row.filter.setTraced(false);
    const auto row = rows.find(address);
    if (row != rows.end()) row->filter.setTraced(editor->isVisible()); // only the one looked at is traced
    editor->setSignal(address, chainOf(address));
}

void SignalPanel::setChainOf(const QString &address, FilterChain chain)
{
    setChain(address, std::move(chain), true);
    if (address == selected) editor->setSignal(address, chainOf(address));
}

void SignalPanel::setFiltersShown(bool shown)
{
    editor->setVisible(shown);
    const auto row = rows.find(selected);
    if (row != rows.end()) row->filter.setTraced(shown);
}

bool SignalPanel::filtersShown() const { return !editor->isHidden(); }

void SignalPanel::setChain(const QString &address, FilterChain chain, bool save)
{
    auto row = rows.find(address);
    if (row == rows.end()) return;
    row->filter.setChain(std::move(chain));
    showBadge(*row);
    if (!save) return;
    QSettings settings;
    const auto text = chainToText(row->filter.chain());
    if (text.isEmpty()) settings.remove(settingsKey(address) + "/filters");
    else settings.setValue(settingsKey(address) + "/filters", text);
}

// Right-click on a family: a filter for all of its signals, the chain of the selected one for all of them, or none
void SignalPanel::familyMenu(const QString &family, const QPoint &globalPosition)
{
    QStringList members;
    for (auto i = rows.cbegin(); i != rows.cend(); ++i)
        if (signalFamily(i.key()) == family) members << i.key();
    if (members.isEmpty()) return;
    const auto each = [this, members](const std::function<FilterChain(const QString &, FilterChain)> &change) {
        for (const auto &address : members) {
            setChain(address, change(address, chainOf(address)), true);
            if (address == selected) editor->setSignal(address, chainOf(address));
        }
    };
    QMenu menu;
    auto *add = menu.addMenu(QStringLiteral("Add a filter to the %1 signals of %2/*").arg(members.size()).arg(family));
    add->addAction("Invert", this, [each] {
        each([](const QString &address, FilterChain chain) {
            for (const auto &stage : invertChain(address)) chain.push_back(stage);
            return chain;
        });
    });
    add->addSeparator();
    std::vector<InteractionFilter> kinds;
    for (const auto &address : members)
        for (auto kind : filtersOffered(address))
            if (std::find(kinds.begin(), kinds.end(), kind) == kinds.end()) kinds.push_back(kind);
    addFilterActions(add, kinds, [each](InteractionFilter kind) {
        each([kind](const QString &address, FilterChain chain) {
            const auto offered = filtersOffered(address);
            if (std::find(offered.begin(), offered.end(), kind) != offered.end()) chain.push_back(InteractionStage::made(kind));
            return chain;
        });
    });
    auto *copy = menu.addAction(selected.isEmpty() ? QStringLiteral("Give them the chain of the selected signal")
                                                   : QStringLiteral("Give them the chain of %1").arg(selected));
    copy->setEnabled(!selected.isEmpty() && rows.contains(selected));
    connect(copy, &QAction::triggered, this, [this, each, source = chainOf(selected)] {
        each([source](const QString &address, FilterChain) { return adapted(source, address); });
    });
    menu.addAction("Remove their filters", this, [each] {
        each([](const QString &, FilterChain) { return FilterChain{}; });
    });
    menu.exec(globalPosition);
}

QString SignalPanel::update(const QJsonObject &frame, const QString &destinations, bool sendEnabled)
{
    const auto values = signalsFor(frame);
    QList<Destination> targets;
    const bool valid = parseDestinations(destinations, targets);
    const bool armed = sendEnabled && valid;
    const double dt = std::clamp(clock.restart() / 1000.0, .001, .5);
    bool added = false;
    for (auto i = values.begin(); i != values.end(); ++i)
        if (!rows.contains(i.key())) {
            rows.insert(i.key(), makeRow(i.key()));
            added = true;
        }
    if (added) applyQuery();
    int messages = 0, errors = 0;
    for (auto i = rows.begin(); i != rows.end(); ++i) {
        const auto &address = i.key();
        const bool available = values.contains(address);
        const auto type = signalType(address);
        const bool angle = isAngle(address);
        double output = 0;
        if (available) {
            const double raw = values.value(address);
            output = i->filter.step(raw, dt);
            i->raw = signalText(type, angle, raw);
            i->shown = signalText(type, angle, signalValue(type, output).toDouble());
            i->level = signalLevel(type, angle, i->meter->minimum(), i->meter->maximum(), raw); // the bar is the signal itself
        } else {
            if (i->initialized) i->filter.reset(); // when it is back, the filters start from its value
            i->raw = i->shown = QStringLiteral("—");
            i->level = i->meter->minimum();
        }
        i->initialized = available;
        const bool filtered = !i->filter.chain().empty();
        i->meter->setFormat(filtered ? i->shown : i->raw); // the text is what is sent
        i->meter->setValue(i->level);
        i->meter->setFiltered(filtered && available, available ? signalLevel(type, angle, i->meter->minimum(), i->meter->maximum(), output) : 0);
        if (address == selected && editor->isVisible())
            editor->setTrace(available, available ? values.value(address) : 0.0, i->filter.afterEach());
        if (available && signalFired) signalFired(address, signalValue(type, output));
        if (!armed || !i->send->isChecked()) { i->sent = false; continue; }
        if (!available && !i->sent) continue;
        // A signal that is gone is told once as 0 (off)
        const double outgoing = available ? output : 0;
        osc::Message message;
        switch (type) {
        case SignalType::Bool: message = {address, outgoing >= 0.5 ? "T" : "F", {}}; break;
        case SignalType::Int: message = {address, "i", {int(std::lround(outgoing))}}; break;
        case SignalType::Float: message = {address, "f", {outgoing}}; break;
        }
        const auto packet = osc::encode(message);
        bool delivered = true;
        for (const auto &target : targets) {
            if (udp.writeDatagram(packet, target.ip, target.port) < 0) { ++errors; delivered = false; }
            else ++messages;
        }
        if (delivered) i->sent = available;
    }
    QString label = sendEnabled && !valid ? "Invalid OSC destination" :
                    QString("%1 live signals · %2 OSC datagrams").arg(values.size()).arg(messages);
    for (const auto &key : frame.keys())
        if (key == "error" || key.endsWith("_error")) label += " · " + frame.value(key).toString();
    if (errors) label += QString(" · %1 send errors").arg(errors);
    return label;
}
