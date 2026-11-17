#include "About.h"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {
struct Credit { const char *name, *url, *license; };

const Credit credits[] = {
    {"Qt 6", "https://www.qt.io", "LGPL-3.0 — interface, camera and video"},
    {"MediaPipe", "https://ai.google.dev/edge/mediapipe", "Apache-2.0 — hand, face and body tracking and its models, by Google"},
    {"Python (python-build-standalone)", "https://github.com/astral-sh/python-build-standalone", "PSF-2.0 — portable runtime for the tracking worker"},
    {"NumPy", "https://numpy.org", "BSD-3-Clause"},
    {"OpenCV", "https://opencv.org", "Apache-2.0 — a MediaPipe dependency"},
#ifdef Q_OS_MACOS
    {"Syphon", "https://github.com/Syphon/Syphon-Framework", "BSD-2-Clause — video sharing between macOS apps"},
#endif
#ifdef Q_OS_WIN
    {"Spout2", "https://github.com/leadedge/Spout2", "BSD-2-Clause — video sharing between Windows apps"},
#endif
    {"NyxMap", "https://github.com/PixelStereo/NyxMap", "AGPL-3.0 — the OSC codec and tracking worker come from it"},
};
} // namespace

void showAbout(QWidget *parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle("About MpOsc");
    auto *layout = new QVBoxLayout(&dialog);
    layout->setSpacing(10);

    auto *header = new QHBoxLayout;
    auto *icon = new QLabel;
    icon->setPixmap(QApplication::windowIcon().pixmap(80, 80));
    icon->setAlignment(Qt::AlignTop);
    header->addWidget(icon);
    auto *title = new QLabel(QStringLiteral(
        "<h2 style='margin:0'>MpOsc</h2><p style='margin:2px 0'>Motion Processing OSC · %1</p>"
        "<p style='margin:6px 0 0'>Turn movement into live OSC controls.<br>"
        "Built by <a href=\"https://pixelstereo.com\">Pixel Stereo</a>.</p>").arg(QApplication::applicationVersion()));
    title->setTextFormat(Qt::RichText);
    title->setOpenExternalLinks(true);
    title->setWordWrap(true);
    header->addWidget(title, 1);
    layout->addLayout(header);

    QString html = QStringLiteral(
        "<h3 style='margin:0 0 4px'>License</h3>"
        "<p style='margin:0'>MpOsc is free software, released under the "
        "<a href=\"https://www.gnu.org/licenses/gpl-3.0.html\">GNU General Public License, version 3</a>. "
        "You may use, study, share and modify it under the terms of that license; it comes without any warranty. "
        "The full text is the <code>LICENSE</code> file shipped with the application and in the "
        "<a href=\"https://github.com/PixelStereo/MpOsc\">source repository</a>.</p>"
        "<h3 style='margin:12px 0 4px'>Credits</h3>"
        "<p style='margin:0 0 4px'>MpOsc is built on these libraries. Each keeps its own license, "
        "shipped alongside it in the application.</p><ul style='margin:0'>");
    for (const auto &credit : credits)
        html += QStringLiteral("<li><a href=\"%1\">%2</a> — %3</li>").arg(credit.url, credit.name, credit.license);
    html += QStringLiteral("</ul>");
    auto *details = new QLabel(html);
    details->setTextFormat(Qt::RichText);
    details->setOpenExternalLinks(true);
    details->setWordWrap(true);
    details->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    auto *scroll = new QScrollArea;
    scroll->setWidget(details);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    layout->addWidget(scroll, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.resize(520, 480);
    dialog.exec();
}
