#include "ui/AboutPage.h"

#include "AppPaths.h"
#include "Icons.h"
#include "Theme.h"
#include "ui/UiKit.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#ifndef LUMACAPTURE_BUILD_ID
#define LUMACAPTURE_BUILD_ID "local"
#endif

namespace luma::app {
namespace {

const char* kGitHub = "https://github.com/etcofficials/LumaCapture";
const char* kIssues = "https://github.com/etcofficials/LumaCapture/issues";
const char* kReleases = "https://github.com/etcofficials/LumaCapture/releases";
const char* kInstagram = "https://www.instagram.com/etcofficials";
const char* kMail = "mailto:etcofficials28@gmail.com";

QPushButton* linkButton(IconId icon, const QString& text, const QString& url, const QString& tip, QWidget* parent)
{
    auto* b = new QPushButton(makeIcon(icon, currentPalette().text), text, parent);
    b->setToolTip(tip.isEmpty() ? url : tip);
    b->setCursor(Qt::PointingHandCursor);
    QObject::connect(b, &QPushButton::clicked, parent, [url] { QDesktopServices::openUrl(QUrl(url)); });
    return b;
}

} // namespace

QString AboutPage::version()
{
    return QStringLiteral(LUMACAPTURE_VERSION);
}

QString AboutPage::buildInfo()
{
    return QStringLiteral("build %1, %2").arg(QStringLiteral(LUMACAPTURE_BUILD_ID), QStringLiteral(__DATE__));
}

AboutPage::AboutPage(QWidget* parent) : QWidget(parent)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(12);

    // Identity
    QVBoxLayout* body = nullptr;
    QFrame* id = ui::panel(this, body);
    auto* head = new QHBoxLayout;
    head->setSpacing(14);
    auto* logo = new QLabel(id);
    logo->setPixmap(makeIcon(IconId::App, Qt::white).pixmap(56, 56));
    auto* names = new QVBoxLayout;
    names->setSpacing(2);
    auto* title = new QLabel(QStringLiteral("LumaCapture"), id);
    title->setObjectName("PageTitle");
    auto* ver = new QLabel(QStringLiteral("Version %1").arg(version()), id);
    ver->setObjectName("Value");
    auto* tagline = ui::dim(QStringLiteral("Lightweight screen recording for Windows PCs."), id);
    auto* build = ui::hint(buildInfo(), id);
    build->setTextInteractionFlags(Qt::TextSelectableByMouse);
    names->addWidget(title);
    names->addWidget(ver);
    names->addWidget(tagline);
    names->addWidget(build);
    head->addWidget(logo, 0, Qt::AlignTop);
    head->addLayout(names, 1);
    body->addLayout(head);
    auto* links = new QHBoxLayout;
    links->addWidget(linkButton(IconId::Link, QStringLiteral("GitHub"), QString::fromLatin1(kGitHub),
                                QStringLiteral("Source code and documentation"), id));
    links->addWidget(linkButton(IconId::Import, QStringLiteral("Check for updates"), QString::fromLatin1(kReleases),
                                QStringLiteral("Opens the Releases page in your browser (LumaCapture itself never "
                                               "connects to the internet)"),
                                id));
    auto* guide = new QPushButton(makeIcon(IconId::Help, currentPalette().text), QStringLiteral("User guide"), id);
    links->addWidget(guide);
    links->addStretch();
    body->addLayout(links);
    v->addWidget(id);

    // Feedback
    QVBoxLayout* fb = nullptr;
    QFrame* feedback = ui::panel(this, fb, QStringLiteral("Feedback"));
    fb->addWidget(ui::dim(QStringLiteral("Found a bug or have a suggestion?"), feedback));
    auto* fl = new QHBoxLayout;
    fl->addWidget(linkButton(IconId::Link, QStringLiteral("Instagram: @etcofficials"), QString::fromLatin1(kInstagram), {},
                             feedback));
    fl->addWidget(linkButton(IconId::Mail, QStringLiteral("etcofficials28@gmail.com"), QString::fromLatin1(kMail),
                             QStringLiteral("Write an e-mail"), feedback));
    fl->addWidget(linkButton(IconId::Warning, QStringLiteral("Report a bug"), QString::fromLatin1(kIssues),
                             QStringLiteral("GitHub issues - please attach the newest log (Settings > Advanced > Open logs)"),
                             feedback));
    fl->addStretch();
    fb->addLayout(fl);
    v->addWidget(feedback);

    // License
    QVBoxLayout* lb = nullptr;
    QFrame* license = ui::panel(this, lb, QStringLiteral("License"));
    lb->addWidget(ui::hint(QStringLiteral("LumaCapture is free software under the GNU General Public License v3.0. It "
                                          "includes FFmpeg (GPL-3.0 build with x264) and Qt 6 (LGPL-3.0). LumaCapture "
                                          "contains no telemetry and makes no network connections."),
                           license));
    auto* licenses = new QPushButton(makeIcon(IconId::Folder, currentPalette().text),
                                     QStringLiteral("Licenses and third-party notices"), license);
    lb->addWidget(licenses, 0, Qt::AlignLeft);
    v->addWidget(license);
    v->addStretch();

    connect(guide, &QPushButton::clicked, this, [] {
        const QString readme = AppPaths::docsDir() + QStringLiteral("/README.txt");
        QDesktopServices::openUrl(QFileInfo::exists(readme) ? QUrl::fromLocalFile(readme)
                                                            : QUrl(QString::fromLatin1(kGitHub)));
    });
    connect(licenses, &QPushButton::clicked, this, [] {
        const QString dir = AppPaths::licensesDir();
        QDesktopServices::openUrl(QFileInfo::exists(dir) ? QUrl::fromLocalFile(dir)
                                                         : QUrl(QString::fromLatin1(kGitHub) + QStringLiteral("/blob/main/LICENSE")));
    });
}

} // namespace luma::app
