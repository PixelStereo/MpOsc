#include <pxs/FilterEditor.h>
#include "HoverHints.h"
#include "OscCodec.h"
#include "SignalFilters.h"
#include "SignalPanel.h"
#include "Theme.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkDatagram>
#include <QJsonObject>
#include <QCheckBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QLabel>
#include <QMimeData>
#include <QListWidget>
#include <QTimer>
#include <QProgressBar>
#include <QPushButton>
#include <QToolButton>
#include <QToolTip>
#include <QTreeWidget>
#include <QtTest>

namespace {
// A frame of the worker, with a jaw that opens to `jaw`
QJsonObject frame(double jaw)
{
    return QJsonObject{{"face", QJsonArray{QJsonObject{{"expressions", QJsonObject{{"jawOpen", jaw}}}}}}};
}

FilterChain slowChain()
{
    auto smooth = InteractionStage::made(InteractionFilter::AttackRelease);
    smooth.setting(0) = 0.5; // attack, seconds
    smooth.setting(1) = 0.5; // release
    auto hold = InteractionStage::made(InteractionFilter::Average);
    return {smooth, hold};
}
}

class UiTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("PixelStereoTest");
        QCoreApplication::setApplicationName("MpOscUiTest");
        QSettings().clear();
        applyInterfacePalette(*qobject_cast<QApplication *>(QCoreApplication::instance()));
    }

    void init() { QSettings().clear(); } // each test starts without the filters the last one saved

    void theTraceEndsWhereTheChainEnds()
    {
        SignalFilter filter;
        filter.setChain(slowChain());
        filter.setTraced(true);
        double out = 0;
        for (int i = 0; i < 25; ++i) out = filter.step(i < 5 ? 0.0 : 1.0, .033);
        QCOMPARE(filter.afterEach().size(), size_t(2));
        QVERIFY(std::abs(filter.afterEach().back() - out) < 1e-9); // the last bar is what is sent
        filter.setChain({});
        QVERIFY(filter.afterEach().empty());
    }

    void aCurveAndAnEnvelopeAreDrawnInTheEditor()
    {
        const auto offered = filtersOffered("/face/jawOpen");
        QVERIFY(std::find(offered.begin(), offered.end(), InteractionFilter::Curve) != offered.end());
        QVERIFY(std::find(offered.begin(), offered.end(), InteractionFilter::Envelope) != offered.end());
        SignalPanel panel;
        panel.resize(900, 700);
        panel.show();
        panel.update(frame(0.0), "127.0.0.1:9", false); // the signal is there
        panel.setFiltersShown(true);
        panel.selectSignal("/face/jawOpen");
        panel.setChainOf("/face/jawOpen", {InteractionStage::made(InteractionFilter::Curve), InteractionStage::made(InteractionFilter::Envelope)});
        auto *editor = panel.findChild<pxs::filterview::FilterEditor *>();
        QVERIFY(editor);
        // The cards are folded: unfold them
        for (auto *fold : editor->findChildren<QToolButton *>())
            if (fold->arrowType() == Qt::RightArrow) fold->click();
        QTest::qWait(50);
        QVERIFY(editor->findChild<QWidget *>("filterCurve0")); // the drawn curve
        QVERIFY(editor->findChild<QWidget *>("adsrCurve"));    // the envelope in the hand
    }

    void filtersAreReorderedByDragging()
    {
        SignalPanel panel;
        panel.resize(900, 700);
        panel.show();
        panel.update(frame(0.0), "127.0.0.1:9", false);
        panel.setFiltersShown(true);
        panel.selectSignal("/face/jawOpen");
        FilterChain three = slowChain();
        three.push_back(InteractionStage::made(InteractionFilter::Threshold));
        panel.setChainOf("/face/jawOpen", three);
        auto *editor = panel.findChild<pxs::filterview::FilterEditor *>();
        auto *list = editor->findChild<QListWidget *>();
        QVERIFY(list && list->count() == 3);
        QTest::qWait(50);
        const auto order = [&] {
            QStringList names;
            for (const auto &stage : panel.chainOf("/face/jawOpen")) names << stage.displayName();
            return names;
        };
        const QStringList before = order();
        QCOMPARE(before.size(), 3);
        // What a drag of the first card carries, dropped below the last (a real drag needs a real mouse: the drag of the platform
        // under test does not start from synthetic events, so the events of the drag are sent to the list as it would get them)
        auto *mime = new QMimeData;
        mime->setData("application/x-pxs-filter", "0");
        const QPoint at = list->visualItemRect(list->item(2)).bottomLeft() + QPoint(40, 6);
        QDragEnterEvent enter(at, Qt::MoveAction, mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(list->viewport(), &enter);
        QVERIFY(enter.isAccepted());
        QDragMoveEvent move(at, Qt::MoveAction, mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(list->viewport(), &move);
        QVERIFY(move.isAccepted());
        QDropEvent drop(at, Qt::MoveAction, mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(list->viewport(), &drop);
        QTest::qWait(300);
        const QStringList after = order();
        QCOMPARE(after.size(), 3);
        QCOMPARE(after.last(), before.first()); // the first filter went to the end
        QCOMPARE(after.first(), before[1]);
        // And the last one back to the top (dropped above the first card); the cards are rebuilt in their new order, with their bars
        auto *up = new QMimeData;
        up->setData("application/x-pxs-filter", "2");
        const QPoint top = list->visualItemRect(list->item(0)).topLeft() + QPoint(40, 2);
        QDragEnterEvent enter2(top, Qt::MoveAction, up, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(list->viewport(), &enter2);
        QDropEvent drop2(top, Qt::MoveAction, up, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(list->viewport(), &drop2);
        QTest::qWait(300);
        QCOMPARE(order(), before);
        QCOMPARE(list->count(), 3);
        QCOMPARE(editor->findChildren<QProgressBar *>("signalValue").size(), 2); // the signal in, the signal out
    }

    void filtersAreHiddenUntilAsked()
    {
        SignalPanel panel;
        panel.show();
        QVERIFY(!panel.filtersShown());
        panel.setFiltersShown(true);
        QVERIFY(panel.filtersShown());
        panel.setFiltersShown(false);
        QVERIFY(!panel.filtersShown());
    }

    void aBarOverTheFirstFilterAndOneUnderEach()
    {
        SignalPanel panel;
        panel.resize(900, 600);
        panel.show();
        panel.update(frame(0.0), "127.0.0.1:9", false);
        panel.setFiltersShown(true);
        panel.selectSignal("/face/jawOpen");
        auto *editor = panel.findChild<pxs::filterview::FilterEditor *>();
        QVERIFY(editor);
        // The signal alone: its bar, and no filter yet
        auto bars = [&] {
            QList<QProgressBar *> shown;
            for (auto *bar : editor->findChildren<QProgressBar *>("signalValue"))
                if (bar->isVisibleTo(editor)) shown << bar;
            return shown;
        };
        QCOMPARE(bars().size(), 1);
        auto gain = InteractionStage::made(InteractionFilter::Math);
        gain.op = MathOp::Multiply;
        gain.a = 0.5;
        panel.setChainOf("/face/jawOpen", {gain});
        panel.update(frame(1.0), "127.0.0.1:9", false);
        const auto all = bars();
        QCOMPARE(all.size(), 2);
        QCOMPARE(all[0]->format(), QString("1.000"));
        QCOMPARE(all[0]->value(), 1000);
        QCOMPARE(all[1]->format(), QString("0.500"));
        QCOMPARE(all[1]->value(), 500);
    }

    void theTableKeepsTheSignalAndShowsTheFilteredOne()
    {
        SignalPanel panel;
        panel.resize(900, 600);
        panel.show();
        panel.update(frame(0.0), "127.0.0.1:9", false);
        panel.setChainOf("/face/jawOpen", slowChain());
        for (int i = 0; i < 12; ++i) {
            panel.update(frame(1.0), "127.0.0.1:9", false);
            QTest::qWait(8);
        }
        // The fill of the bar is the signal as it comes in (1.000); the text is what is sent, and it has not got there
        auto *table = panel.findChild<QTreeWidget *>();
        QVERIFY(table);
        QProgressBar *meter = nullptr;
        for (auto *bar : panel.findChildren<QProgressBar *>("signalValue"))
            if (bar->isVisibleTo(&panel) && bar->value() == bar->maximum()) meter = bar;
        QVERIFY(meter);
        QVERIFY(meter->format() != QString("1.000") && meter->format() != QString("—"));
        if (!qEnvironmentVariable("MPOSC_SHOT").isEmpty()) {
            panel.setFiltersShown(true);
            panel.setSending(true);
            for (auto *box : panel.findChildren<QCheckBox *>()) box->setChecked(true);
            panel.selectSignal("/face/jawOpen");
            for (int i = 0; i < 30; ++i) {
                panel.update(frame(0.5 + 0.5 * std::sin(i / 3.0)), "127.0.0.1:9", false);
                QTest::qWait(10);
            }
            panel.grab().save(qEnvironmentVariable("MPOSC_SHOT"));
        }
    }

    void aSignalCanDriveTheSelectedDeviceAddress()
    {
        QUdpSocket receiver;
        QVERIFY(receiver.bind(QHostAddress::LocalHost, 0));
        pxs::devices::Device mixer;
        mixer.kind = pxs::devices::Kind::Osc;
        mixer.name = "Mixer";
        mixer.host = "127.0.0.1";
        mixer.port = receiver.localPort();
        pxs::devices::Address fader;
        fader.path = "/strip/1/fader";
        mixer.addresses = {fader};
        pxs::devices::Devices devices;
        const auto id = devices.add(mixer);
        QVERIFY(id);
        QSettings settings;
        settings.setValue("devices/registry", QJsonDocument(devices.toJson()).toJson(QJsonDocument::Compact));
        settings.setValue("signals/face/jawOpen/send", true);
        settings.setValue("signals/face/jawOpen/targetDevice", id);
        settings.setValue("signals/face/jawOpen/targetPath", QStringLiteral("/strip/1/fader"));

        SignalPanel panel;
        panel.update(frame(0.75), QStringLiteral("invalid global OSC destination"), true);
        QTRY_VERIFY_WITH_TIMEOUT(receiver.hasPendingDatagrams(), 1000);
        std::vector<osc::Message> messages;
        osc::decode(receiver.receiveDatagram().data(), messages);
        QCOMPARE(messages.size(), size_t(1));
        QCOMPARE(messages.front().address, QStringLiteral("/strip/1/fader"));
        QCOMPARE(messages.front().args.front().toDouble(), 0.75);
    }

    void whatAControlIsForIsSaidInTheStatusBar()
    {
        QLabel label;
        label.resize(400, 20);
        HoverHints::install(&label);
        QPushButton button("Go");
        button.setToolTip("Starts the thing");
        button.show();
        QEnterEvent enter(QPointF(1, 1), QPointF(1, 1), QPointF(1, 1));
        QCoreApplication::sendEvent(&button, &enter);
        QCOMPARE(label.text(), QString("Starts the thing"));
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(&button, &leave);
        QVERIFY(label.text().isEmpty());
        // The pop-up is not shown
        QHelpEvent help(QEvent::ToolTip, QPoint(1, 1), button.mapToGlobal(QPoint(1, 1)));
        QCoreApplication::sendEvent(&button, &help);
        QVERIFY(!QToolTip::isVisible());
    }
};

QTEST_MAIN(UiTest)
#include "ui_test.moc"
