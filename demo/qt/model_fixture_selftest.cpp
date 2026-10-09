#include "model_fixture_selftest.hpp"
#include "wav_io.hpp"

#include <audition/audio/audio_buffer.hpp>

#include <QByteArray>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

[[maybe_unused]] QWidget* requireTab(QTabWidget* tabs, const QString& name) {
    if (tabs == nullptr) {
        throw std::runtime_error{"Missing QTabWidget"};
    }
    for (int index = 0; index < tabs->count(); ++index) {
        if (tabs->tabText(index) == name) {
            return tabs->widget(index);
        }
    }
    throw std::runtime_error{
        "Missing tab: " + name.toStdString()};
}

[[maybe_unused]] QWidget* runnerPage(QMainWindow& window, const QString& name) {
    auto* root = qobject_cast<QTabWidget*>(window.centralWidget());
    auto* workbench = requireTab(root, "Backend workbench");
    auto* tabs = workbench->findChild<QTabWidget*>();
    return requireTab(tabs, name);
}

[[maybe_unused]] QLineEdit* requireFormEdit(QWidget* panel, const QString& fieldName) {
    auto* form = panel->findChild<QFormLayout*>();
    if (form == nullptr) {
        throw std::runtime_error{"Missing backend form"};
    }
    for (int row = 0; row < form->rowCount(); ++row) {
        auto* labelItem = form->itemAt(
            row, QFormLayout::LabelRole);
        auto* label = labelItem == nullptr
            ? nullptr
            : qobject_cast<QLabel*>(labelItem->widget());
        if (label == nullptr || label->text() != fieldName) {
            continue;
        }
        auto* field = form->itemAt(row, QFormLayout::FieldRole);
        if (field == nullptr || field->widget() == nullptr) {
            throw std::runtime_error{"Missing field widget"};
        }
        auto* editor = qobject_cast<QLineEdit*>(field->widget());
        if (editor == nullptr) {
            editor = field->widget()->findChild<QLineEdit*>();
        }
        if (editor == nullptr) {
            throw std::runtime_error{
                "Field lacks QLineEdit: " + fieldName.toStdString()};
        }
        return editor;
    }
    throw std::runtime_error{
        "Missing field: " + fieldName.toStdString()};
}

[[maybe_unused]] QPushButton* requireButton(QWidget* panel, const QString& caption) {
    for (auto* button : panel->findChildren<QPushButton*>()) {
        if (button->text() == caption) {
            return button;
        }
    }
    throw std::runtime_error{
        "Missing button: " + caption.toStdString()};
}

[[maybe_unused]] QPlainTextEdit* requireOutput(QWidget* panel) {
    for (auto* candidate : panel->findChildren<QPlainTextEdit*>()) {
        if (candidate->isReadOnly()) {
            return candidate;
        }
    }
    throw std::runtime_error{"Missing result output widget"};
}

[[maybe_unused]] QString awaitAsyncCompletion(
    QWidget* panel,
    QPushButton* runButton) {
    auto* output = requireOutput(panel);
    auto* progress = panel->findChild<QProgressBar*>("asyncProgress");
    if (progress == nullptr || progress->isHidden() ||
        runButton->isEnabled()) {
        throw std::runtime_error{
            "Inference did not enter asynchronous busy state"};
    }

    bool eventLoopAlive = false;
    // This callback can only execute if the GUI event loop is responsive.
    QTimer::singleShot(0, panel, [&eventLoopAlive]() {
        eventLoopAlive = true;
    });

    QEventLoop loop;
    QTimer poll;
    poll.setInterval(10);
    QObject::connect(
        &poll, &QTimer::timeout,
        &loop, [&]() {
            if (progress->isHidden()) {
                loop.quit();
            }
        });
    bool timedOut = false;
    QTimer::singleShot(120000, &loop, [&]() {
        timedOut = true;
        loop.quit();
    });
    poll.start();
    loop.exec();

    if (timedOut || !progress->isHidden()) {
        throw std::runtime_error{"Async GUI runner timed out"};
    }
    if (!eventLoopAlive || !runButton->isEnabled()) {
        throw std::runtime_error{
            "GUI event loop stalled or action remained disabled"};
    }
    return output->toPlainText();
}

[[maybe_unused]] QString clickAndRead(
    QWidget* panel,
    const QString& buttonText) {
    auto* button = requireButton(panel, buttonText);
    auto* output = requireOutput(panel);
    output->clear();
    button->click();

    const QString result = awaitAsyncCompletion(panel, button);
    if (result.isEmpty() ||
        result.startsWith("error:", Qt::CaseInsensitive)) {
        throw std::runtime_error{
            "Backend runner failed: " + result.toStdString()};
    }
    return result;
}

[[maybe_unused]] std::filesystem::path requireModelEnv(const char* name) {
    const QByteArray value = qgetenv(name);
    if (value.isEmpty()) {
        throw std::runtime_error{
            std::string{"Missing model fixture environment variable: "} +
            name};
    }
    const std::filesystem::path path{value.toStdString()};
    if (!std::filesystem::is_regular_file(path)) {
        throw std::runtime_error{
            "Missing model fixture file: " + path.string()};
    }
    return path;
}

[[maybe_unused]] std::filesystem::path createFixture(
    const QTemporaryDir& dir,
    std::uint32_t rate,
    std::size_t frames) {
    constexpr double pi = 3.14159265358979323846;
    std::vector<float> samples(frames);
    for (std::size_t i = 0U; i < samples.size(); ++i) {
        samples[i] = static_cast<float>(
            0.1 * std::sin(
                2.0 * pi * 440.0 * static_cast<double>(i) /
                static_cast<double>(rate)));
    }
    audition::AudioBuffer audio{
        std::move(samples),
        {rate, 1U, audition::AudioLayout::Interleaved},
        audition::Timestamp{}};
    const std::filesystem::path path{
        dir.filePath(
            QStringLiteral("fixture_%1.wav").arg(rate)).toStdString()};
    demo::saveWavPcm16(path, audio.view());
    return path;
}

[[maybe_unused]] void requireContains(
    const QString& text,
    const QString& fragment) {
    if (!text.contains(fragment)) {
        throw std::runtime_error{
            "Runner response did not contain '" +
            fragment.toStdString() + "':\n" +
            text.toStdString()};
    }
}

QString testClap(QMainWindow& window, const QTemporaryDir& dir) {
#if LIBAUDITION_DEMO_HAS_CLAP
    const auto audioModel =
        requireModelEnv("LIBAUDITION_TEST_CLAP_AUDIO_MODEL");
    const auto textModel =
        requireModelEnv("LIBAUDITION_TEST_CLAP_TEXT_MODEL");
    const auto tokenizer =
        requireModelEnv("LIBAUDITION_TEST_CLAP_TOKENIZER");

    // The pinned CLAP model expects 480000 samples at 48 kHz.
    const auto wav = createFixture(dir, 48000U, 480000U);

    auto* panel = runnerPage(window, "CLAP");
    requireFormEdit(panel, "WAV")->setText(
        QString::fromStdString(wav.string()));
    requireFormEdit(panel, "CLAP audio ONNX")->setText(
        QString::fromStdString(audioModel.string()));
    requireFormEdit(panel, "CLAP text ONNX")->setText(
        QString::fromStdString(textModel.string()));
    requireFormEdit(panel, "Tokenizer JSON")->setText(
        QString::fromStdString(tokenizer.string()));
    requireFormEdit(panel, "Candidate labels")->setText(
        "speech, music, dog barking");

    QElapsedTimer timer;
    timer.start();
    const QString embedding = clickAndRead(panel, "Embed audio");
    const qint64 embeddingMs = timer.elapsed();
    requireContains(embedding, "dimension=512");
    requireContains(embedding, "l2_norm=");

    timer.restart();
    const QString classes =
        clickAndRead(panel, "Open-vocabulary classify");
    const qint64 classificationMs = timer.elapsed();
    requireContains(classes, "speech=");
    requireContains(classes, "music=");
    requireContains(classes, "dog barking=");
    requireContains(
        classes, "probabilities are normalized only");

    return QStringLiteral(
        "qt-model-self-test=ok backend=clap "
        "embedding_ms=%1 classification_ms=%2")
        .arg(embeddingMs)
        .arg(classificationMs);
#else
    static_cast<void>(window);
    static_cast<void>(dir);
    throw std::runtime_error{
        "Qt demo was built without LIBAUDITION_WITH_CLAP"};
#endif
}

QString testAasist(QMainWindow& window, const QTemporaryDir& dir) {
#if LIBAUDITION_DEMO_HAS_AASIST
    const auto model = requireModelEnv("LIBAUDITION_TEST_AASIST_MODEL");
    const auto wav = createFixture(dir, 16000U, 16000U);

    auto* panel = runnerPage(window, "AASIST");
    requireFormEdit(panel, "WAV")->setText(
        QString::fromStdString(wav.string()));
    requireFormEdit(panel, "AASIST ONNX")->setText(
        QString::fromStdString(model.string()));

    QElapsedTimer timer;
    timer.start();
    const QString result = clickAndRead(panel, "Run AASIST");
    const qint64 elapsedMs = timer.elapsed();
    requireContains(result, "bona_fide_score=");
    requireContains(result, "spoof_score=");
    requireContains(result, "bona_fide_probability=<uncalibrated>");
    requireContains(result, "spoof_probability=<uncalibrated>");

    // Cancellation is intentionally non-interrupting: discard the result,
    // let the backend finish, then ensure the panel becomes reusable.
    auto* action = requireButton(panel, "Run AASIST");
    auto* discard = requireButton(panel, "Discard result");
    action->click();
    if (!discard->isEnabled()) {
        throw std::runtime_error{
            "Discard result was not enabled while inference ran"};
    }
    discard->click();
    const QString discarded = awaitAsyncCompletion(panel, action);
    requireContains(discarded, "Result discarded.");
    if (discard->isEnabled()) {
        throw std::runtime_error{
            "Discard result remained enabled after completion"};
    }


    return QStringLiteral(
        "qt-model-self-test=ok backend=aasist inference_ms=%1")
        .arg(elapsedMs);
#else
    static_cast<void>(window);
    static_cast<void>(dir);
    throw std::runtime_error{
        "Qt demo was built without LIBAUDITION_WITH_AASIST"};
#endif
}

QString testSilero(QMainWindow& window, const QTemporaryDir& dir) {
#if LIBAUDITION_DEMO_HAS_SHERPA
    const auto model = requireModelEnv("LIBAUDITION_TEST_SILERO_MODEL");
    const auto wav = createFixture(dir, 16000U, 32000U);

    auto* sherpa = runnerPage(window, "Sherpa");
    auto* panel = requireTab(
        sherpa->findChild<QTabWidget*>(), "VAD");
    requireFormEdit(panel, "WAV")->setText(
        QString::fromStdString(wav.string()));
    requireFormEdit(panel, "VAD model")->setText(
        QString::fromStdString(model.string()));

    QElapsedTimer timer;
    timer.start();
    const QString result = clickAndRead(panel, "Run VAD");
    const qint64 elapsedMs = timer.elapsed();
    requireContains(result, "preferred_frame_count=");
    requireContains(result, "processed_blocks=");
    requireContains(result, "speech_active_blocks=");
    requireContains(result, "last_probability=");

    return QStringLiteral(
        "qt-model-self-test=ok backend=silero_vad inference_ms=%1")
        .arg(elapsedMs);
#else
    static_cast<void>(window);
    static_cast<void>(dir);
    throw std::runtime_error{
        "Qt demo was built without LIBAUDITION_WITH_SHERPA"};
#endif
}

QString testSherpaAsyncFailures(QMainWindow& window) {
#if LIBAUDITION_DEMO_HAS_SHERPA
    auto* sherpa = runnerPage(window, "Sherpa");
    auto* tabs = sherpa->findChild<QTabWidget*>();
    auto* asr = requireTab(tabs, "Offline ASR");
    auto* button = requireButton(asr, "Run offline Whisper ASR");

    // Exercise worker error propagation, completion, and panel reuse without
    // requiring an unpinned Whisper model asset.
    button->click();
    const QString first = awaitAsyncCompletion(asr, button);
    requireContains(first, "error:");
    button->click();
    const QString second = awaitAsyncCompletion(asr, button);
    requireContains(second, "error:");
    for (const auto& item : {
             std::pair<const char*, const char*>{
                 "Streaming ASR", "Run streaming ASR"},
             {"KWS", "Run keyword spotting"}}) {
        auto* panel = requireTab(tabs, item.first);
        auto* action = requireButton(panel, item.second);
        action->click();
        const QString error = awaitAsyncCompletion(panel, action);
        requireContains(error, "error:");
        if (!action->isEnabled()) {
            throw std::runtime_error{
                "Sherpa action remained disabled after worker failure"};
        }
    }

#if LIBAUDITION_DEMO_HAS_SHERPA_TTS
    auto* tts = requireTab(tabs, "VITS / Piper TTS");
    auto* synth = requireButton(tts, "Synthesize with VITS/Piper");
    synth->click();
    const QString ttsError = awaitAsyncCompletion(tts, synth);
    requireContains(ttsError, "error:");
    return "qt-model-self-test=ok backend=sherpa-asr-tts-error-paths";
#else
    return "qt-model-self-test=ok backend=sherpa-asr-error-path";
#endif
#else
    static_cast<void>(window);
    throw std::runtime_error{
        "Qt demo was built without LIBAUDITION_WITH_SHERPA"};
#endif
}

}  // namespace

bool runQtModelFixtureSelfTest(
    QMainWindow& window,
    const QString& which,
    QString* report) {
    try {
        QTemporaryDir dir;
        if (!dir.isValid()) {
            throw std::runtime_error{
                "Unable to create temporary model-fixture directory"};
        }
        QString result;
        if (which == "clap") {
            result = testClap(window, dir);
        } else if (which == "aasist") {
            result = testAasist(window, dir);
        } else if (which == "silero") {
            result = testSilero(window, dir);
        } else if (which == "sherpa-errors") {
            result = testSherpaAsyncFailures(window);
        } else {
            throw std::runtime_error{
                "Unknown model fixture (expected clap, aasist, silero or sherpa-errors)"};
        }
        if (report != nullptr) {
            *report = result;
        }
        return true;
    } catch (const std::exception& error) {
        if (report != nullptr) {
            *report = QString::fromUtf8(error.what());
        }
        return false;
    }
}
