#include "model_fixture_selftest.hpp"
#include "wav_io.hpp"

#include <audition/audio/audio_buffer.hpp>

#include <QByteArray>
#include <QComboBox>
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

    for (const auto& item : {
             std::pair<const char*, const char*>{
                 "Language ID", "Identify language"},
             {"Audio tagging", "Run audio tagging"},
             {"Denoise", "Run denoiser"}}) {
        auto* panel = requireTab(tabs, item.first);
        auto* action = requireButton(panel, item.second);
        action->click();
        const QString error = awaitAsyncCompletion(panel, action);
        requireContains(error, "error:");
        if (!action->isEnabled()) {
            throw std::runtime_error{
                "Sherpa secondary runner remained disabled after failure"};
        }
    }

    // All speaker panels must keep the Qt event loop responsive while
    // model/WAV validation and inference run off-thread.
    for (const auto& item : {
             std::pair<const char*, const char*>{
                 "Speaker embedding", "Extract speaker embedding"},
             {"Speaker verification", "Verify speaker"},
             {"Speaker identification", "Build index and identify"},
             {"Diarization", "Run speaker diarization"}}) {
        auto* panel = requireTab(tabs, item.first);
        auto* action = requireButton(panel, item.second);
        action->click();
        const QString error = awaitAsyncCompletion(panel, action);
        requireContains(error, "error:");
        if (!action->isEnabled()) {
            throw std::runtime_error{
                "Speaker panel did not recover after worker failure"};
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

QString testWorld(QMainWindow& window, const QTemporaryDir& dir) {
#if LIBAUDITION_DEMO_HAS_WORLD
    const auto wav = createFixture(dir, 16000U, 32000U);
    auto* panel = runnerPage(window, "WORLD");
    requireFormEdit(panel, "WAV")->setText(
        QString::fromStdString(wav.string()));
    const QString result = clickAndRead(panel, "Run WORLD analysis");
    requireContains(result, "pitch_mean_hz=");
    requireContains(result, "frame_count=");
    requireContains(result, "frequency_bins=");
    return "qt-model-self-test=ok backend=world";
#else
    static_cast<void>(window);
    static_cast<void>(dir);
    throw std::runtime_error{"Qt demo was built without WORLD"};
#endif
}

QString testAlsaCaptureErrors(QMainWindow& window) {
#if LIBAUDITION_DEMO_HAS_ALSA
    auto* root = qobject_cast<QTabWidget*>(window.centralWidget());
    auto* panel = requireTab(root, "Acoustic scene");
    auto* device = panel->findChild<QComboBox*>("acousticSceneDevice");
    auto* state = panel->findChild<QLabel*>("acousticSceneStatus");
    if (device == nullptr || state == nullptr) {
        throw std::runtime_error{"ALSA live controls missing"};
    }
    auto* start = requireButton(panel, "Start live capture");
    auto* stop = requireButton(panel, "Stop live capture");
    device->setEditText("hw:254,0"); // impossible ALSA card, no hardware necessary

    for (int attempt = 0; attempt < 2; ++attempt) {
        start->click();
        if (start->isEnabled() || !stop->isEnabled()) {
            throw std::runtime_error{"Capture did not enter asynchronous state"};
        }
        QEventLoop loop;
        QTimer poll;
        bool responsive = false;
        bool completed = false;
        QTimer::singleShot(0, &loop, [&responsive]() { responsive = true; });
        QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
            if (state->text().startsWith("Live ALSA/ODAS error:")) {
                completed = true;
                loop.quit();
            }
        });
        QTimer::singleShot(10000, &loop, [&]() { loop.quit(); });
        poll.start(20);
        loop.exec();
        if (!completed || !responsive || !start->isEnabled() ||
            stop->isEnabled()) {
            throw std::runtime_error{
                "ALSA open failure did not return control to responsive GUI: " +
                state->text().toStdString()};
        }
    }
    return "qt-model-self-test=ok backend=alsa-live-error-restart";
#else
    static_cast<void>(window);
    throw std::runtime_error{"Qt demo was built without ALSA live capture"};
#endif
}

QString testOdasErrors(QMainWindow& window) {
#if LIBAUDITION_DEMO_HAS_ODAS
    auto* panel = runnerPage(window, "ODAS");
    auto* action = requireButton(panel, "Run ODAS over WAV");
    action->click();
    const QString first = awaitAsyncCompletion(panel, action);
    requireContains(first, "error:");
    action->click();
    const QString second = awaitAsyncCompletion(panel, action);
    requireContains(second, "error:");
    return "qt-model-self-test=ok backend=odas-error-path";
#else
    static_cast<void>(window);
    throw std::runtime_error{"Qt demo was built without ODAS"};
#endif
}

[[maybe_unused]] QPlainTextEdit* requireFormMultiline(
    QWidget* panel, const QString& name) {
    auto* form = panel->findChild<QFormLayout*>();
    if (form == nullptr) {
        throw std::runtime_error{"Missing form in optional panel"};
    }
    for (int i = 0; i < form->rowCount(); ++i) {
        auto* labelItem = form->itemAt(i, QFormLayout::LabelRole);
        const auto* label = labelItem
            ? qobject_cast<QLabel*>(labelItem->widget()) : nullptr;
        if (label == nullptr || label->text() != name) {
            continue;
        }
        auto* field = form->itemAt(i, QFormLayout::FieldRole);
        auto* edit = field
            ? qobject_cast<QPlainTextEdit*>(field->widget()) : nullptr;
        if (edit != nullptr) {
            return edit;
        }
    }
    throw std::runtime_error{
        "Missing multiline field: " + name.toStdString()};
}

QString testSamplerate(QMainWindow& window, const QTemporaryDir& dir) {
#if LIBAUDITION_DEMO_HAS_SAMPLERATE
    const auto wav = createFixture(dir, 16000U, 16000U);
    const auto saved =
        std::filesystem::path{dir.filePath("resampled.wav").toStdString()};
    auto* panel = runnerPage(window, "libsamplerate");
    requireFormEdit(panel, "Input WAV")->setText(
        QString::fromStdString(wav.string()));
    requireFormEdit(panel, "Output WAV")->setText(
        QString::fromStdString(saved.string()));
    const auto result = clickAndRead(panel, "Resample WAV");
    requireContains(result, "saved=");
    requireContains(result, "flush_frames=");
    const auto output = demo::loadWav(saved);
    if (output.audio.format().sample_rate_hz != 48000U ||
        output.audio.frameCount() < 47000U ||
        output.audio.frameCount() > 49000U) {
        throw std::runtime_error{"Unexpected resampling output format"};
    }
    return "qt-model-self-test=ok backend=samplerate";
#else
    static_cast<void>(window);
    static_cast<void>(dir);
    throw std::runtime_error{"Qt demo was built without libsamplerate"};
#endif
}

QString testAec3(QMainWindow& window, const QTemporaryDir& dir) {
#if LIBAUDITION_DEMO_HAS_AEC3
    const auto wav = createFixture(dir, 16000U, 3200U);
    const auto saved =
        std::filesystem::path{dir.filePath("aec3.wav").toStdString()};
    auto* panel = runnerPage(window, "AEC3");
    requireFormEdit(panel, "Captured WAV")->setText(
        QString::fromStdString(wav.string()));
    requireFormEdit(panel, "Playback/reference WAV")->setText(
        QString::fromStdString(wav.string()));
    requireFormEdit(panel, "Output WAV")->setText(
        QString::fromStdString(saved.string()));
    const auto result = clickAndRead(panel, "Run AEC3");
    requireContains(result, "processed_blocks=");
    requireContains(result, "echo_return_loss_db=");
    const auto output = demo::loadWav(saved);
    if (output.audio.format().sample_rate_hz != 16000U ||
        output.audio.frameCount() != 3200U) {
        throw std::runtime_error{"AEC3 produced unexpected WAV dimensions"};
    }
    return "qt-model-self-test=ok backend=aec3";
#else
    static_cast<void>(window);
    static_cast<void>(dir);
    throw std::runtime_error{"Qt demo was built without WebRTC AEC3"};
#endif
}

QString testGtsam(QMainWindow& window) {
#if LIBAUDITION_DEMO_HAS_GTSAM
    auto* panel = runnerPage(window, "GTSAM");
    requireFormMultiline(panel, "Position observations")->setPlainText(
        "2,0,0,0.04,0.04,0.04\n"
        "2.1,0.1,0,0.09,0.09,0.09");
    const QString result = clickAndRead(panel, "Fuse observations");
    requireContains(result, "backend=");
    requireContains(result, "positions=2");
    requireContains(result, "result=");
    return "qt-model-self-test=ok backend=gtsam";
#else
    static_cast<void>(window);
    throw std::runtime_error{"Qt demo was built without GTSAM"};
#endif
}


[[maybe_unused]] std::filesystem::path requireDirectoryEnv(const char* name) {
    const QByteArray value = qgetenv(name);
    if (value.isEmpty()) {
        throw std::runtime_error{
            std::string{"Missing fixture environment variable: "} + name};
    }
    const std::filesystem::path path{value.toStdString()};
    if (!std::filesystem::is_directory(path)) {
        throw std::runtime_error{
            "Missing fixture directory: " + path.string()};
    }
    return path;
}

[[maybe_unused]] void setModelPath(
    QWidget* panel, const QString& field, const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path)) {
        throw std::runtime_error{
            "Missing fixture file: " + path.string()};
    }
    requireFormEdit(panel, field)->setText(
        QString::fromStdString(path.string()));
}

QString testWhisperReal(QMainWindow& window, const QTemporaryDir& dir) {
#if LIBAUDITION_DEMO_HAS_SHERPA
    const auto encoder = requireModelEnv("LIBAUDITION_TEST_WHISPER_ENCODER");
    const auto decoder = requireModelEnv("LIBAUDITION_TEST_WHISPER_DECODER");
    const auto tokens = requireModelEnv("LIBAUDITION_TEST_WHISPER_TOKENS");
    const auto wav = createFixture(dir, 16000U, 32000U);

    auto* sherpa = runnerPage(window, "Sherpa");
    auto* panel = requireTab(
        sherpa->findChild<QTabWidget*>(), "Offline ASR");
    setModelPath(panel, "WAV", wav);
    setModelPath(panel, "Whisper encoder", encoder);
    setModelPath(panel, "Whisper decoder", decoder);
    setModelPath(panel, "Tokens", tokens);
    requireFormEdit(panel, "Language")->setText("en");

    const QString result = clickAndRead(panel, "Run offline Whisper ASR");
    // Synthetic tone may produce an empty transcript. Here the invariant is
    // successful real ONNX execution and a structurally valid response, not WER.
    requireContains(result, "text=");
    requireContains(result, "language=");
    requireContains(result, "tokens=");
    requireContains(result, "confidence=");
    return "qt-model-self-test=ok backend=whisper-real-onnx";
#else
    static_cast<void>(window);
    static_cast<void>(dir);
    throw std::runtime_error{"Qt demo was built without Sherpa"};
#endif
}

QString testPiperReal(QMainWindow& window, const QTemporaryDir& dir) {
#if LIBAUDITION_DEMO_HAS_SHERPA_TTS
    const auto root = requireDirectoryEnv("LIBAUDITION_TEST_PIPER_DIR");
    std::filesystem::path model;
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (entry.is_regular_file() &&
            entry.path().extension() == ".onnx") {
            if (!model.empty()) {
                throw std::runtime_error{"Piper fixture has multiple ONNX models"};
            }
            model = entry.path();
        }
    }
    if (model.empty()) {
        throw std::runtime_error{"Piper fixture has no ONNX model"};
    }
    const auto tokens = root / "tokens.txt";
    const auto data = root / "espeak-ng-data";
    if (!std::filesystem::is_directory(data)) {
        throw std::runtime_error{
            "Missing Piper espeak-ng-data: " + data.string()};
    }
    const auto saved = std::filesystem::path{
        dir.filePath("piper_generated.wav").toStdString()};
    auto* sherpa = runnerPage(window, "Sherpa");
    auto* panel = requireTab(
        sherpa->findChild<QTabWidget*>(), "VITS / Piper TTS");
    setModelPath(panel, "VITS/Piper model", model);
    setModelPath(panel, "Tokens", tokens);
    requireFormEdit(panel, "espeak-ng data")->setText(
        QString::fromStdString(data.string()));
    requireFormEdit(panel, "Text")->setText("Hello from libaudition.");
    requireFormEdit(panel, "Language")->setText("en");
    requireFormEdit(panel, "Output WAV")->setText(
        QString::fromStdString(saved.string()));

    const QString result = clickAndRead(
        panel, "Synthesize with VITS/Piper");
    requireContains(result, "sample_rate_hz=");
    requireContains(result, "frames=");
    requireContains(result, "saved=");
    const auto wav = demo::loadWav(saved);
    if (wav.audio.format().sample_rate_hz < 8000U ||
        wav.audio.format().channel_count != 1U ||
        wav.audio.frameCount() < 1000U) {
        throw std::runtime_error{
            "Piper generated no valid mono speech waveform"};
    }
    return "qt-model-self-test=ok backend=piper-real-onnx";
#else
    static_cast<void>(window);
    static_cast<void>(dir);
    throw std::runtime_error{"Qt demo was built without Sherpa TTS"};
#endif
}

QString testSpeakerReal(QMainWindow& window, const QTemporaryDir& dir) {
#if LIBAUDITION_DEMO_HAS_SHERPA
    const auto model = requireModelEnv("LIBAUDITION_TEST_SPEAKER_MODEL");
    const auto wav = createFixture(dir, 16000U, 80000U);
    auto* sherpa = runnerPage(window, "Sherpa");
    auto* tabs = sherpa->findChild<QTabWidget*>();
    auto* embedding = requireTab(tabs, "Speaker embedding");
    setModelPath(embedding, "Input WAV", wav);
    setModelPath(embedding, "Speaker embedding model", model);
    const QString extracted = clickAndRead(
        embedding, "Extract speaker embedding");
    requireContains(extracted, "dimension=");
    requireContains(extracted, "l2_norm=");

    auto* verification = requireTab(tabs, "Speaker verification");
    setModelPath(verification, "Reference WAV", wav);
    setModelPath(verification, "Candidate WAV", wav);
    setModelPath(verification, "Speaker embedding model", model);
    const QString verified = clickAndRead(
        verification, "Verify speaker");
    requireContains(verified, "similarity=");
    requireContains(verified, "matched=yes");

    auto* identification = requireTab(tabs, "Speaker identification");
    setModelPath(identification, "Speaker embedding model", model);
    setModelPath(identification, "Query WAV", wav);
    requireFormMultiline(identification, "Enrollments")->setPlainText(
        QStringLiteral("Fixture speaker|%1|0").arg(
            QString::fromStdString(wav.string())));
    const QString identified = clickAndRead(
        identification, "Build index and identify");
    requireContains(identified, "enrolled_speakers=1");
    requireContains(identified, "name=Fixture speaker");
    return "qt-model-self-test=ok backend=speaker-real-onnx";
#else
    static_cast<void>(window);
    static_cast<void>(dir);
    throw std::runtime_error{"Qt demo was built without Sherpa"};
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
        } else if (which == "world") {
            result = testWorld(window, dir);
        } else if (which == "odas-errors") {
            result = testOdasErrors(window);
        } else if (which == "alsa-errors") {
            result = testAlsaCaptureErrors(window);
        } else if (which == "samplerate") {
            result = testSamplerate(window, dir);
        } else if (which == "aec3") {
            result = testAec3(window, dir);
        } else if (which == "gtsam") {
            result = testGtsam(window);
        } else if (which == "whisper-real") {
            result = testWhisperReal(window, dir);
        } else if (which == "piper-real") {
            result = testPiperReal(window, dir);
        } else if (which == "speaker-real") {
            result = testSpeakerReal(window, dir);
        } else {
            throw std::runtime_error{
                "Unknown model fixture; see docs/qt_demo.md for valid scenario names"};
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
