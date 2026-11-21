#include "FilterEditor.h"
#include "HoverHints.h"
#include "SignalFilters.h"
#include "SignalPanel.h"
#include "Theme.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QCheckBox>
#include <QLabel>
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
        auto *editor = panel.findChild<FilterEditor *>();
        QVERIFY(editor);
        // The signal alone: its bar, and no filter yet
        auto bars = [&] { return editor->findChildren<QProgressBar *>("signalValue"); };
        QCOMPARE(bars().size(), 1);
        panel.setChainOf("/face/jawOpen", slowChain());
        QCOMPARE(bars().size(), 1 + 2); // the signal as it comes in, then the signal after each filter
        for (int i = 0; i < 40; ++i) {
            panel.update(frame(i < 5 ? 0.0 : 1.0), "127.0.0.1:9", false);
            QTest::qWait(8);
        }
        const auto all = bars();
        // The top one is the signal itself; the others, what each filter makes of it: the slow smoothing is behind, the signal is not
        QCOMPARE(all.first()->format(), QString("1.000"));
        QVERIFY(all[1]->value() < all.first()->value());
        QVERIFY(all[1]->format() != all.first()->format());
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
