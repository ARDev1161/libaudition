#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <initializer_list>
#include <vector>

class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QVBoxLayout;
class QWidget;

template <typename T>
class QFutureWatcher;

// Manages one long-running GUI action on Qt's thread pool.
// The work function MUST NOT touch widgets or capture their pointers.
// "Discard result" is cooperative UI cancellation: model inference itself
// cannot be forcibly interrupted through the current backend API.
class AsyncPanelRunner final : public QObject {
public:
    AsyncPanelRunner(
        QWidget* page,
        QVBoxLayout* layout,
        std::initializer_list<QPushButton*> actions,
        QPlainTextEdit* output);

    void start(std::function<QString()> work);
    [[nodiscard]] bool busy() const noexcept;

private:
    void finish();
    void discardResult();

    QFutureWatcher<QString>* watcher_;
    QLabel* status_;
    QProgressBar* progress_;
    QPushButton* discard_;
    QPlainTextEdit* output_;
    std::vector<QPushButton*> actions_;
    bool busy_{false};
    bool discarded_{false};
};
