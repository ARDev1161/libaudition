#include "async_panel_runner.hpp"

#include <QFutureWatcher>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent/QtConcurrentRun>

#include <exception>
#include <stdexcept>
#include <utility>

AsyncPanelRunner::AsyncPanelRunner(
    QWidget* page,
    QVBoxLayout* layout,
    std::initializer_list<QPushButton*> actions,
    QPlainTextEdit* output)
    : QObject{page},
      watcher_{new QFutureWatcher<QString>{this}},
      status_{new QLabel{page}},
      progress_{new QProgressBar{page}},
      discard_{new QPushButton{"Discard result", page}},
      output_{output},
      actions_{actions} {
    if (layout == nullptr || output_ == nullptr || actions_.empty()) {
        throw std::invalid_argument{"Invalid async runner UI"};
    }

    status_->setObjectName("asyncStatus");
    status_->setWordWrap(true);
    status_->setText("Idle");
    progress_->setObjectName("asyncProgress");
    progress_->setRange(0, 0);  // No percentage available from ONNX backends.
    progress_->hide();
    discard_->setObjectName("discardResult");
    discard_->setEnabled(false);

    layout->addWidget(status_);
    layout->addWidget(progress_);
    layout->addWidget(discard_);

    QObject::connect(
        discard_, &QPushButton::clicked,
        this, [this]() { discardResult(); });
    QObject::connect(
        watcher_, &QFutureWatcher<QString>::finished,
        this, [this]() { finish(); });
}

bool AsyncPanelRunner::busy() const noexcept {
    return busy_;
}

void AsyncPanelRunner::start(std::function<QString()> work) {
    if (busy_) {
        return;  // Prevent overlapping work on the same backend panel.
    }
    if (!work) {
        throw std::invalid_argument{"Missing async work function"};
    }

    busy_ = true;
    discarded_ = false;
    for (auto* button : actions_) {
        button->setEnabled(false);
    }
    discard_->setEnabled(true);
    status_->setText(
        "Running in background (indeterminate progress; "
        "the backend does not expose a completion percentage).");
    progress_->show();
    output_->setPlainText("Running…");

    // The closure owns a snapshot of inputs. No QWidget/QObject access occurs
    // on the worker thread, including when the panel is destroyed meanwhile.
    watcher_->setFuture(QtConcurrent::run(
        [work = std::move(work)]() -> QString {
            try {
                return work();
            } catch (const std::exception& error) {
                return QStringLiteral("error: ") +
                    QString::fromUtf8(error.what());
            } catch (...) {
                return QStringLiteral("error: unknown backend exception");
            }
        }));
}

void AsyncPanelRunner::discardResult() {
    if (!busy_) {
        return;
    }
    discarded_ = true;
    discard_->setEnabled(false);
    status_->setText(
        "Result discarded. Waiting for the backend to finish; "
        "ONNX inference cannot be interrupted safely.");
    output_->setPlainText("Result discarded (backend still running).");
}

void AsyncPanelRunner::finish() {
    if (!busy_) {
        return;
    }

    // QFutureWatcher delivers finished on its owning (GUI) thread.
    const QString result = watcher_->result();
    busy_ = false;
    progress_->hide();
    discard_->setEnabled(false);
    for (auto* button : actions_) {
        button->setEnabled(true);
    }

    if (discarded_) {
        status_->setText("Idle (previous result discarded)");
        output_->setPlainText("Result discarded.");
    } else {
        status_->setText("Idle");
        output_->setPlainText(result);
    }
}
