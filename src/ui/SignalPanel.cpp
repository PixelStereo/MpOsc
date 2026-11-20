#include "SignalPanel.h"

#include "Destinations.h"
#include "OscCodec.h"
#include "SignalWidgets.h"
#include "Signals.h"
#include "SliderField.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QStyle>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

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
        for (auto &row : rows) row.signal->setChecked(true);
    });
    connect(disableAll, &QPushButton::clicked, this, [this] {
        for (auto &row : rows) row.signal->setChecked(false);
    });
    connect(search, &QLineEdit::textChanged, this, [this](const QString &text) {
        query = text;
        for (auto i = rows.begin(); i != rows.end(); ++i)
            if (i->item->treeWidget() == table) i->item->setHidden(!i.key().contains(query, Qt::CaseInsensitive));
    });
    const auto makeTable = [] {
        auto *view = new QTreeWidget;
        view->setRootIsDecorated(false);
        view->setSelectionMode(QAbstractItemView::NoSelection);
        view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        view->setHeaderLabels({"Signal / OSC address", "Raw", "Invert", "Attack", "Release", "Value"});
        view->header()->setStretchLastSection(false);
        view->setColumnWidth(0, 195);
        view->setColumnWidth(1, 75);
        view->setColumnWidth(2, 56);
        for (int column : {3, 4}) view->setColumnWidth(column, 104);
        view->setColumnWidth(5, 85);
        view->header()->setStretchLastSection(true);
        view->setSortingEnabled(true);
        view->sortItems(0, Qt::AscendingOrder);
        view->header()->setSortIndicatorShown(false);
        view->header()->setSectionsClickable(false);
        return view;
    };
    pinnedTable = makeTable();
    table = makeTable();
    pinnedTable->hide();
    controls->addWidget(pinnedTable);
    controls->addWidget(table, 1);
    for (auto *view : {table, pinnedTable}) {
        auto *other = view == table ? pinnedTable : table;
        connect(view->horizontalScrollBar(), &QScrollBar::valueChanged,
                other->horizontalScrollBar(), &QScrollBar::setValue);
        connect(view->header(), &QHeaderView::sectionResized, other, [other](int column, int, int width) {
            if (column != 5 && other->columnWidth(column) != width) other->setColumnWidth(column, width);
        });
    }
    clock.start();
}

QString SignalPanel::filter() const { return search->text(); }
void SignalPanel::setFilter(const QString &text) { search->setText(text); }

void SignalPanel::setSending(bool enabled)
{
    sending = enabled;
    for (auto &row : rows) {
        row.sent = false;
        row.meter->setEnabled(sending && row.signal->isChecked());
    }
}

void SignalPanel::resetSent()
{
    for (auto &row : rows) row.sent = false;
}

void SignalPanel::resetValues()
{
    for (auto &row : rows) row.initialized = row.sent = false;
}

void SignalPanel::clear()
{
    rows.clear();
    table->clear();
    pinnedTable->clear();
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
}

void SignalPanel::resizePinnedTable()
{
    const int count = pinnedTable->topLevelItemCount();
    pinnedTable->setVisible(count > 0);
    table->setHeaderHidden(count > 0);
    if (count > 0) {
        const int rowHeight = std::max(26, pinnedTable->visualItemRect(pinnedTable->topLevelItem(0)).height());
        pinnedTable->setFixedHeight(pinnedTable->header()->sizeHint().height() +
                                   std::min(count, 6) * rowHeight + 2 * pinnedTable->frameWidth() +
                                   pinnedTable->horizontalScrollBar()->sizeHint().height());
    }
}

void SignalPanel::moveSignal(const QString &address, bool pinned)
{
    auto row = rows.find(address);
    if (row == rows.end() || (row->item->treeWidget() == pinnedTable) == pinned) return;
    const auto previous = *row;
    const int value = previous.meter->value();
    const auto format = previous.meter->format();
    const auto raw = previous.raw->text();
    const double attack = previous.attack->value();
    const double release = previous.release->value();
    delete previous.item;
    *row = makeRow(address);
    row->filtered = previous.filtered;
    row->initialized = previous.initialized;
    row->sent = previous.sent;
    row->attack->setValue(attack);
    row->release->setValue(release);
    row->raw->setText(raw);
    row->meter->setFormat(format);
    row->meter->setValue(value);
    resizePinnedTable();
}

void SignalPanel::setSignalKnown(std::function<void(const QString &)> callback)
{
    signalKnown = std::move(callback);
    if (signalKnown)
        for (auto i = rows.cbegin(); i != rows.cend(); ++i) signalKnown(i.key());
}

SignalPanel::Row SignalPanel::makeRow(const QString &address)
{
    if (signalKnown) signalKnown(address);
    const auto settingsKey = QStringLiteral("signals") + address;
    QSettings settings;
    const bool pinned = settings.value(settingsKey + "/pinned", false).toBool();
    auto *view = pinned ? pinnedTable : table;
    auto *item = new SignalItem(view, address);
    item->setHidden(!pinned && !address.contains(query, Qt::CaseInsensitive));
    auto *signal = new QPushButton(address);
    signal->setCheckable(true);
    signal->setChecked(settings.value(settingsKey + "/send", false).toBool());
    signal->setObjectName("signalName");
    auto *nameCell = new QFrame;
    nameCell->setObjectName("signalCell");
    const auto updateOutline = [nameCell](bool enabled) {
        nameCell->setProperty("oscEnabled", enabled);
        nameCell->style()->unpolish(nameCell);
        nameCell->style()->polish(nameCell);
        nameCell->update();
    };
    updateOutline(signal->isChecked());
    connect(signal, &QPushButton::toggled, nameCell, updateOutline);
    auto *nameLayout = new QHBoxLayout(nameCell);
    nameLayout->setContentsMargins(1, 1, 1, 1);
    nameLayout->setSpacing(4);
    auto *pin = new PinButton;
    pin->setChecked(pinned);
    pin->setToolTip(pinned ? "Unpin signal" : "Pin signal");
    pin->setAccessibleName(pin->toolTip());
    connect(pin, &QToolButton::toggled, this, [this, address, settingsKey](bool enabled) {
        QSettings().setValue(settingsKey + "/pinned", enabled);
        moveSignal(address, enabled);
    }, Qt::QueuedConnection);
    signal->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    signal->setToolTip(address);
    nameLayout->addWidget(pin, 0, Qt::AlignVCenter);
    nameLayout->addWidget(signal, 1);
    view->setItemWidget(item, 0, nameCell);

    auto *raw = new QLabel(QStringLiteral("—"));
    raw->setAlignment(Qt::AlignCenter);
    view->setItemWidget(item, 1, raw);

    auto *invert = new QCheckBox;
    invert->setAccessibleName("Invert " + address);
    invert->setToolTip("Invert before Attack and Release");
    invert->setChecked(settings.value(settingsKey + "/invert", false).toBool());
    auto *invertCell = new QWidget;
    auto *invertLayout = new QHBoxLayout(invertCell);
    invertLayout->setContentsMargins(0, 0, 0, 0);
    invertLayout->addWidget(invert, 0, Qt::AlignCenter);
    view->setItemWidget(item, 2, invertCell);
    connect(invert, &QCheckBox::toggled, this, [settingsKey](bool enabled) {
        QSettings().setValue(settingsKey + "/invert", enabled);
    });

    auto *meter = new QProgressBar;
    if (address.startsWith("/face/head/")) {
        const int limit = address.endsWith("/pitch") ? 90 : 180;
        meter->setRange(-limit, limit);
    } else {
        meter->setRange(0, 1000);
    }
    meter->setEnabled(sending && signal->isChecked());
    meter->setObjectName("signalValue");
    view->setItemWidget(item, 5, meter);
    connect(signal, &QPushButton::toggled, this, [this, address, settingsKey, meter](bool enabled) {
        rows[address].sent = false;
        meter->setEnabled(sending && enabled);
        QSettings().setValue(settingsKey + "/send", enabled);
    });

    const auto addTimeField = [&](int column, const QString &name) {
        const auto key = settingsKey + name;
        auto *field = new SliderField;
        field->setDecimals(3);
        field->setRange(0, 2);
        field->setTypedRange(0, 60);
        field->setSingleStep(.01);
        field->setSuffix(" s");
        field->setValue(settings.value(key, settings.value(settingsKey + "/smooth", 0).toDouble() / 1000).toDouble());
        connect(field, &SliderField::editingFinished, this, [key](double value) {
            QSettings().setValue(key, value);
        });
        view->setItemWidget(item, column, field);
        return field;
    };
    auto row = Row{item, meter, raw, invert, signal, addTimeField(3, "/attack"), addTimeField(4, "/release")};
    if (signalType(address) == SignalType::Bool) { // a switch is not smoothed
        row.attack->setEnabled(false);
        row.release->setEnabled(false);
    }
    if (pinned) resizePinnedTable();
    return row;
}

QString SignalPanel::update(const QJsonObject &frame, const QString &destinations, bool sendEnabled)
{
    const auto values = signalsFor(frame);
    QList<Destination> targets;
    const bool valid = parseDestinations(destinations, targets);
    const bool armed = sendEnabled && valid;
    const double dt = std::clamp(clock.restart() / 1000.0, .001, .5);
    for (auto i = values.begin(); i != values.end(); ++i)
        if (!rows.contains(i.key())) rows.insert(i.key(), makeRow(i.key()));
    int messages = 0, errors = 0;
    for (auto i = rows.begin(); i != rows.end(); ++i) {
        const bool available = values.contains(i.key());
        const double raw = values.value(i.key());
        const bool angle = i->meter->minimum() < 0;
        const bool wraps = angle && i->meter->maximum() == 180;
        const auto formatValue = [angle, available](double value) {
            if (!available) return QStringLiteral("—");
            auto text = QString::number(value, 'f', angle ? 1 : 3);
            if (angle) text += QStringLiteral("°");
            return text;
        };
        const SignalType type = signalType(i.key());
        if (type == SignalType::Bool) { // a switch: on or off, nothing to smooth
            const bool on = (raw >= 0.5) != i->invert->isChecked();
            i->raw->setText(available ? (raw >= 0.5 ? QStringLiteral("on") : QStringLiteral("off")) : QStringLiteral("—"));
            i->filtered = on ? 1 : 0;
            i->initialized = available;
            i->meter->setValue(on ? 1000 : 0);
            i->meter->setFormat(available ? (on ? QStringLiteral("on") : QStringLiteral("off")) : QStringLiteral("—"));
            if (!armed || !i->signal->isChecked()) { i->sent = false; continue; }
            if (!available && !i->sent) continue;
            const auto packet = osc::encode({i.key(), (available ? on : false) ? "T" : "F", {}});
            bool delivered = true;
            for (const auto &target : targets) {
                if (udp.writeDatagram(packet, target.ip, target.port) < 0) { ++errors; delivered = false; }
                else ++messages;
            }
            if (delivered) i->sent = available;
            continue;
        }
        i->raw->setText(formatValue(raw));
        const double input = i->invert->isChecked() ? invertedSignal(i.key(), raw) : raw;
        const double difference = input - i->filtered;
        const double delta = wraps ? std::remainder(difference, 360.0) : difference;
        const double tau = delta > 0 ? i->attack->value() : i->release->value();
        i->filtered = !i->initialized || tau == 0 ? input : i->filtered + delta * -std::expm1(-dt / tau);
        if (wraps) i->filtered = std::remainder(i->filtered, 360.0);
        i->initialized = available;
        const double output = i->filtered;
        i->meter->setValue(qRound(angle
            ? std::clamp(output, double(i->meter->minimum()), double(i->meter->maximum()))
            : 1000 * std::clamp(output, 0.0, 1.0)));
        i->meter->setFormat(formatValue(output));
        if (available && signalFired) signalFired(i.key(), output);
        if (!armed || !i->signal->isChecked()) { i->sent = false; continue; }
        if (!available && !i->sent) continue;
        const double outgoing = available ? output : 0;
        const auto packet = type == SignalType::Int ? osc::encode({i.key(), "i", {qRound(outgoing)}})
                                                    : osc::encode({i.key(), "f", {outgoing}});
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
