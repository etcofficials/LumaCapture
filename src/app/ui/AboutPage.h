#pragma once

#include <QWidget>

namespace luma::app {

// Help / About: version, build, license, GitHub, feedback (Instagram, e-mail),
// bug reports, "check for updates" (opens the Releases page in the browser - the
// app itself makes no network connections) and the user guide.
class AboutPage : public QWidget {
    Q_OBJECT
public:
    explicit AboutPage(QWidget* parent = nullptr);

    static QString version();   // "2.0.0"
    static QString buildInfo(); // "build abc1234, Oct  6 2026"
};

} // namespace luma::app
