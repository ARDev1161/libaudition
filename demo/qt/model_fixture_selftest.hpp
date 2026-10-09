#pragma once

class QMainWindow;
class QString;

// Run a real GUI click-through using existing CI model fixtures.
// No inference is faked; all work goes through the workbench panels.
[[nodiscard]] bool runQtModelFixtureSelfTest(
    QMainWindow& window,
    const QString& which,
    QString* report);
