#include "backend_workbench.hpp"
#include "async_panel_runner.hpp"

#include "wav_io.hpp"
#include "optional_backend_workbench.hpp"
#include "sherpa_extended_workbench.hpp"
#include "sherpa_speaker_workbench.hpp"

#include <audition/audition.hpp>

#if LIBAUDITION_DEMO_HAS_WORLD
#include <audition/backends/world.hpp>
#endif

#if LIBAUDITION_DEMO_HAS_AASIST
#include <audition/backends/aasist.hpp>
#endif

#if LIBAUDITION_DEMO_HAS_CLAP
#include <audition/backends/clap.hpp>
#endif

#if LIBAUDITION_DEMO_HAS_SHERPA
#include <audition/backends/sherpa.hpp>
#endif

#if LIBAUDITION_DEMO_HAS_ODAS
#include <audition/backends/odas.hpp>
#endif

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

QString exceptionText(const std::exception& error) {
    return QStringLiteral("error: ") +
           QString::fromUtf8(error.what());
}

QLabel* description(const QString& text) {
    auto* label = new QLabel{text};
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

QPlainTextEdit* outputBox() {
    auto* output = new QPlainTextEdit;
    output->setReadOnly(true);
    output->setMinimumHeight(220);
    return output;
}

[[maybe_unused]] QDoubleSpinBox* doubleBox(
    double minimum,
    double maximum,
    double value,
    int decimals = 3,
    double step = 0.1) {
    auto* box = new QDoubleSpinBox;
    box->setRange(minimum, maximum);
    box->setValue(value);
    box->setDecimals(decimals);
    box->setSingleStep(step);
    return box;
}

QSpinBox* intBox(
    int minimum,
    int maximum,
    int value) {
    auto* box = new QSpinBox;
    box->setRange(minimum, maximum);
    box->setValue(value);
    return box;
}

QWidget* pathEditor(
    QLineEdit*& edit,
    QWidget* parent,
    bool directory = false,
    const QString& filter = QString{}) {
    auto* row = new QWidget{parent};
    auto* layout = new QHBoxLayout{row};
    layout->setContentsMargins(0, 0, 0, 0);

    edit = new QLineEdit;
    auto* browse = new QPushButton{
        directory ? "Directory…" : "Browse…"};

    layout->addWidget(edit, 1);
    layout->addWidget(browse);

    QObject::connect(
        browse,
        &QPushButton::clicked,
        row,
        [edit, directory, filter]() {
            QString selected;
            if (directory) {
                selected =
                    QFileDialog::getExistingDirectory(
                        nullptr,
                        "Select directory",
                        edit->text());
            } else {
                selected =
                    QFileDialog::getOpenFileName(
                        nullptr,
                        "Select file",
                        edit->text(),
                        filter);
            }
            if (!selected.isEmpty()) {
                edit->setText(selected);
            }
        });

    return row;
}

std::filesystem::path fsPath(const QLineEdit* edit) {
    return std::filesystem::path{
        edit->text().toStdString()};
}

demo::LoadedWav loadWavFrom(
    const QLineEdit* wav_path) {
    if (wav_path->text().isEmpty()) {
        throw std::runtime_error{"Select a WAV file first"};
    }
    return demo::loadWav(fsPath(wav_path));
}

audition::AudioBuffer monoFrom(
    const demo::LoadedWav& wav,
    int channel) {
    if (channel < 0) {
        throw std::runtime_error{"Channel index must be non-negative"};
    }
    return demo::selectMonoChannel(
        wav.audio.view(),
        static_cast<std::size_t>(channel));
}

[[maybe_unused]] QString wavSummary(const demo::LoadedWav& wav) {
    const auto format = wav.audio.format();
    return QStringLiteral(
               "%1 Hz, %2 ch, %3 frames, WAV tag %4, %5 bit")
        .arg(format.sample_rate_hz)
        .arg(format.channel_count)
        .arg(static_cast<qulonglong>(wav.audio.frameCount()))
        .arg(wav.info.format_tag)
        .arg(wav.info.bits_per_sample);
}

QWidget* unavailablePanel(
    const QString& backend,
    const QString& option,
    QWidget* parent) {
    auto* page = new QWidget{parent};
    auto* layout = new QVBoxLayout{page};
    layout->addWidget(description(
        QStringLiteral(
            "<h3>%1 backend is not built</h3>"
            "<p>Reconfigure libaudition with <code>%2=ON</code> "
            "and rebuild the Qt demo. The rest of libaudition "
            "remains usable without this backend.</p>")
            .arg(backend, option)));
    layout->addStretch();
    return page;
}

QWidget* createWavInspector(QWidget* parent) {
    auto* page = new QWidget{parent};
    auto* layout = new QVBoxLayout{page};

    layout->addWidget(description(
        "Open an uncompressed RIFF/WAV file. The demo loader supports "
        "PCM 8/16/24/32-bit and IEEE float32. It never resamples or "
        "downmixes implicitly. Backend panels explicitly select one "
        "channel when a mono contract is required."));

    auto* form = new QFormLayout;
    QLineEdit* wavPath = nullptr;
    auto* channel = intBox(0, 255, 0);
    form->addRow(
        "WAV",
        pathEditor(
            wavPath,
            page,
            false,
            "WAV audio (*.wav *.WAV);;All files (*)"));
    form->addRow("Inspect mono channel", channel);
    layout->addLayout(form);

    auto* inspect = new QPushButton{"Inspect WAV"};
    auto* output = outputBox();
    layout->addWidget(inspect);
    layout->addWidget(output);

    QObject::connect(
        inspect,
        &QPushButton::clicked,
        page,
        [=]() {
            try {
                const auto wav = loadWavFrom(wavPath);
                const auto mono =
                    monoFrom(wav, channel->value());
                const auto quality =
                    audition::dsp::analyzeQuality(
                        mono.view());

                std::ostringstream text;
                text << "file="
                     << wav.info.path.string()
                     << "\n"
                     << "format="
                     << wav.audio.format().sample_rate_hz
                     << " Hz / "
                     << wav.audio.format().channel_count
                     << " channels\n"
                     << "frames="
                     << wav.audio.frameCount()
                     << "\n"
                     << "source_format_tag="
                     << wav.info.format_tag
                     << "\n"
                     << "bits_per_sample="
                     << wav.info.bits_per_sample
                     << "\n"
                     << "selected_channel="
                     << channel->value()
                     << "\n\n"
                     << std::fixed
                     << std::setprecision(6)
                     << "rms="
                     << quality.aggregate.rms_linear
                     << "\n"
                     << "rms_dbfs="
                     << quality.aggregate.rms_dbfs
                     << "\n"
                     << "peak="
                     << quality.aggregate.peak_linear
                     << "\n"
                     << "dc="
                     << quality.aggregate.dc_offset
                     << "\n"
                     << "clipping_ratio="
                     << quality.aggregate.clipping_ratio;
                output->setPlainText(
                    QString::fromStdString(text.str()));
            } catch (const std::exception& error) {
                output->setPlainText(
                    exceptionText(error));
            }
        });

    return page;
}

QWidget* createWorldPanel(QWidget* parent) {
#if LIBAUDITION_DEMO_HAS_WORLD
    auto* page = new QWidget{parent};
    auto* layout = new QVBoxLayout{page};

    layout->addWidget(description(
        "Run the real WORLD backend on one explicitly selected WAV "
        "channel. The panel reports pitch statistics and frame-level "
        "CheapTrick/D4C dimensions."));

    auto* form = new QFormLayout;
    QLineEdit* wavPath = nullptr;
    auto* channel = intBox(0, 255, 0);
    auto* algorithm = new QComboBox;
    algorithm->addItems({"DIO + StoneMask", "Harvest"});
    auto* f0Floor = doubleBox(20.0, 1000.0, 50.0);
    auto* f0Ceil = doubleBox(50.0, 3000.0, 800.0);
    auto* framePeriod = doubleBox(1.0, 100.0, 5.0);

    form->addRow(
        "WAV",
        pathEditor(
            wavPath,
            page,
            false,
            "WAV audio (*.wav *.WAV);;All files (*)"));
    form->addRow("Channel", channel);
    form->addRow("F0 algorithm", algorithm);
    form->addRow("F0 floor [Hz]", f0Floor);
    form->addRow("F0 ceiling [Hz]", f0Ceil);
    form->addRow("Frame period [ms]", framePeriod);
    layout->addLayout(form);

    auto* run = new QPushButton{"Run WORLD analysis"};
    auto* output = outputBox();
    layout->addWidget(run);
    layout->addWidget(output);

    QObject::connect(
        run,
        &QPushButton::clicked,
        page,
        [=]() {
            try {
                const auto wav = loadWavFrom(wavPath);
                auto mono =
                    monoFrom(wav, channel->value());

                audition::WorldVoiceTraitsOptions traitsOptions{};
                audition::WorldAcousticAnalysisOptions acousticOptions{};

                const auto selectedAlgorithm =
                    algorithm->currentIndex() == 0
                        ? audition::WorldF0Algorithm::DioStoneMask
                        : audition::WorldF0Algorithm::Harvest;

                traitsOptions.algorithm = selectedAlgorithm;
                traitsOptions.f0_floor_hz = f0Floor->value();
                traitsOptions.f0_ceil_hz = f0Ceil->value();
                traitsOptions.frame_period_ms =
                    framePeriod->value();

                acousticOptions.algorithm = selectedAlgorithm;
                acousticOptions.f0_floor_hz = f0Floor->value();
                acousticOptions.f0_ceil_hz = f0Ceil->value();
                acousticOptions.frame_period_ms =
                    framePeriod->value();

                audition::WorldVoiceTraitsEstimator traits{
                    traitsOptions};
                audition::WorldAcousticAnalyzer analyzer{
                    acousticOptions};

                const auto estimated =
                    traits.estimate(mono.view());
                const auto features =
                    analyzer.analyze(mono.view());

                std::ostringstream text;
                text << "input="
                     << wavSummary(wav).toStdString()
                     << "\n"
                     << "backend="
                     << traits.backendInfo().name
                     << "\n"
                     << "pitch_mean_hz=";
                if (estimated.pitch_mean_hz.has_value()) {
                    text << *estimated.pitch_mean_hz;
                } else {
                    text << "<absent>";
                }
                text << "\npitch_stddev_hz=";
                if (estimated.pitch_stddev_hz.has_value()) {
                    text << *estimated.pitch_stddev_hz;
                } else {
                    text << "<absent>";
                }

                const auto voiced =
                    static_cast<std::size_t>(
                        std::count(
                            features.voiced_mask.begin(),
                            features.voiced_mask.end(),
                            static_cast<std::uint8_t>(1U)));

                text << "\nframe_count="
                     << features.frame_count
                     << "\nvoiced_frames="
                     << voiced
                     << "\nfft_size="
                     << features.fft_size
                     << "\nfrequency_bins="
                     << features.frequency_bin_count;

                text << "\nfirst_f0_hz=";
                const std::size_t preview =
                    std::min<std::size_t>(
                        features.f0_hz.size(),
                        12U);
                for (std::size_t index = 0U;
                     index < preview;
                     ++index) {
                    if (index != 0U) {
                        text << ", ";
                    }
                    text << std::fixed
                         << std::setprecision(1)
                         << features.f0_hz[index];
                }

                output->setPlainText(
                    QString::fromStdString(text.str()));
            } catch (const std::exception& error) {
                output->setPlainText(
                    exceptionText(error));
            }
        });

    return page;
#else
    return unavailablePanel(
        "WORLD",
        "-DLIBAUDITION_WITH_WORLD",
        parent);
#endif
}

QWidget* createAasistPanel(QWidget* parent) {
#if LIBAUDITION_DEMO_HAS_AASIST
    auto* page = new QWidget{parent};
    auto* layout = new QVBoxLayout{page};

    layout->addWidget(description(
        "Run the real AASIST ONNX waveform backend. The current "
        "contract requires mono 16 kHz input and at most 64600 "
        "frames. Raw bona-fide/spoof logits are scores, not "
        "probabilities unless explicit Platt calibration is enabled."));

    auto* form = new QFormLayout;
    QLineEdit* wavPath = nullptr;
    QLineEdit* modelPath = nullptr;
    auto* channel = intBox(0, 255, 0);
    auto* calibrated = new QCheckBox{"Enable Platt calibration"};
    auto* slope = doubleBox(-100.0, 100.0, 1.0);
    auto* intercept = doubleBox(-100.0, 100.0, 0.0);

    form->addRow(
        "WAV",
        pathEditor(
            wavPath,
            page,
            false,
            "WAV audio (*.wav *.WAV);;All files (*)"));
    form->addRow("Channel", channel);
    form->addRow(
        "AASIST ONNX",
        pathEditor(
            modelPath,
            page,
            false,
            "ONNX model (*.onnx);;All files (*)"));
    form->addRow(calibrated);
    form->addRow("Platt slope", slope);
    form->addRow("Platt intercept", intercept);
    layout->addLayout(form);

    auto* run = new QPushButton{"Run AASIST"};
    auto* output = outputBox();
    layout->addWidget(run);
    layout->addWidget(output);

    auto* runner = new AsyncPanelRunner{page, layout, {run}, output};
    QObject::connect(
        run,
        &QPushButton::clicked,
        page,
        [=]() {
            // Read widgets only on the GUI thread; the worker owns copies.
            const auto wavFile = fsPath(wavPath);
            const auto selectedChannel = channel->value();
            audition::AasistOnnxOptions options{};
            options.model = fsPath(modelPath);
            if (calibrated->isChecked()) {
                options.calibration =
                    audition::AasistPlattCalibration{
                        slope->value(),
                        intercept->value()};
            }

            runner->start(
                [wavFile, selectedChannel, options]() -> QString {
                    const auto wav = demo::loadWav(wavFile);
                    const auto mono = demo::selectMonoChannel(
                        wav.audio.view(),
                        static_cast<std::size_t>(selectedChannel));

                    audition::AasistAuthenticityDetector detector{options};
                    const auto result = detector.analyze(mono.view());

                    std::ostringstream text;
                    text << "input="
                         << wavSummary(wav).toStdString()
                         << "\nbackend="
                         << detector.backendInfo().name
                         << "\nbona_fide_score=";
                    if (result.bona_fide_score.has_value()) {
                        text << result.bona_fide_score->value;
                    } else {
                        text << "<absent>";
                    }
                    text << "\nspoof_score=";
                    if (result.spoof_score.has_value()) {
                        text << result.spoof_score->value;
                    } else {
                        text << "<absent>";
                    }
                    text << "\nbona_fide_probability=";
                    if (result.bona_fide_probability.has_value()) {
                        text << result.bona_fide_probability->value();
                    } else {
                        text << "<uncalibrated>";
                    }
                    text << "\nspoof_probability=";
                    if (result.spoof_probability.has_value()) {
                        text << result.spoof_probability->value();
                    } else {
                        text << "<uncalibrated>";
                    }
                    return QString::fromStdString(text.str());
                });
        });

    return page;
#else
    return unavailablePanel(
        "AASIST",
        "-DLIBAUDITION_WITH_AASIST",
        parent);
#endif
}

#if LIBAUDITION_DEMO_HAS_CLAP
audition::AudioEmbedding runClapEmbedding(
    const audition::AudioBuffer& mono,
    const std::filesystem::path& audio_model) {
    audition::ClapOnnxAudioOptions options;
    options.model = audio_model;
    audition::ClapAudioEmbedder embedder{options};
    return embedder.embed(mono.view());
}

audition::ClassificationResult runClapClassification(
    const audition::AudioBuffer& mono,
    const std::filesystem::path& audio_model,
    const std::filesystem::path& text_model,
    const std::filesystem::path& tokenizer_path,
    double temperature,
    const std::vector<std::string>& candidates) {
    audition::ClapOpenVocabularyOptions options;
    options.audio.model = audio_model;
    options.text.model = text_model;
    options.text.tokenizer = tokenizer_path;
    options.similarity_temperature = temperature;

    audition::ClapOpenVocabularyClassifier classifier{options};
    return classifier.classify(mono.view(), candidates);
}
#endif

QWidget* createClapPanel(QWidget* parent) {
#if LIBAUDITION_DEMO_HAS_CLAP
    auto* page = new QWidget{parent};
    auto* layout = new QVBoxLayout{page};

    layout->addWidget(description(
        "Run the CLAP audio embedder or open-vocabulary classifier. "
        "The backend contract requires mono 48 kHz audio; the demo "
        "does not silently resample the WAV."));

    auto* form = new QFormLayout;
    QLineEdit* wavPath = nullptr;
    QLineEdit* audioModel = nullptr;
    QLineEdit* textModel = nullptr;
    QLineEdit* tokenizer = nullptr;
    auto* channel = intBox(0, 255, 0);
    auto* labels =
        new QLineEdit{"speech, music, dog barking"};
    auto* temperature =
        doubleBox(0.001, 100.0, 1.0, 3, 0.1);

    form->addRow(
        "WAV",
        pathEditor(
            wavPath,
            page,
            false,
            "WAV audio (*.wav *.WAV);;All files (*)"));
    form->addRow("Channel", channel);
    form->addRow(
        "CLAP audio ONNX",
        pathEditor(
            audioModel,
            page,
            false,
            "ONNX model (*.onnx);;All files (*)"));
    form->addRow(
        "CLAP text ONNX",
        pathEditor(
            textModel,
            page,
            false,
            "ONNX model (*.onnx);;All files (*)"));
    form->addRow(
        "Tokenizer JSON",
        pathEditor(
            tokenizer,
            page,
            false,
            "JSON (*.json);;All files (*)"));
    form->addRow("Candidate labels", labels);
    form->addRow("Softmax temperature", temperature);
    layout->addLayout(form);

    auto* buttons = new QWidget{page};
    auto* buttonLayout = new QHBoxLayout{buttons};
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    auto* embed = new QPushButton{"Embed audio"};
    auto* classify = new QPushButton{"Open-vocabulary classify"};
    buttonLayout->addWidget(embed);
    buttonLayout->addWidget(classify);
    layout->addWidget(buttons);

    auto* output = outputBox();
    layout->addWidget(output);

    // Both CLAP actions share one execution slot: repeated clicks cannot
    // cause overlapping model loads or concurrent updates to this panel.
    auto* runner = new AsyncPanelRunner{
        page, layout, {embed, classify}, output};

    QObject::connect(
        embed,
        &QPushButton::clicked,
        page,
        [=]() {
            const auto wavFile = fsPath(wavPath);
            const int selectedChannel = channel->value();
            const auto modelFile = fsPath(audioModel);
            runner->start(
                [wavFile, selectedChannel, modelFile]() -> QString {
                    const auto wav = demo::loadWav(wavFile);
                    const auto mono = demo::selectMonoChannel(
                        wav.audio.view(),
                        static_cast<std::size_t>(selectedChannel));
                    const auto result = runClapEmbedding(mono, modelFile);

                    double norm2 = 0.0;
                    for (const float value : result.values) {
                        norm2 +=
                            static_cast<double>(value) *
                            static_cast<double>(value);
                    }

                    std::ostringstream text;
                    text << "input="
                         << wavSummary(wav).toStdString()
                         << "\nmodel_id="
                         << result.model_id
                         << "\ndimension="
                         << result.values.size()
                         << "\nl2_norm="
                         << std::sqrt(norm2)
                         << "\nquality=";
                    if (result.quality.has_value()) {
                        text << result.quality->value();
                    } else {
                        text << "<absent>";
                    }
                    text << "\nfirst_values=";
                    const std::size_t preview =
                        std::min<std::size_t>(result.values.size(), 10U);
                    for (std::size_t index = 0U;
                         index < preview;
                         ++index) {
                        if (index != 0U) {
                            text << ", ";
                        }
                        text << result.values[index];
                    }
                    return QString::fromStdString(text.str());
                });
        });

    QObject::connect(
        classify,
        &QPushButton::clicked,
        page,
        [=]() {
            std::vector<std::string> candidates{};
            for (const auto& part :
                 labels->text().split(',', Qt::SkipEmptyParts)) {
                candidates.push_back(part.trimmed().toStdString());
            }
            const auto wavFile = fsPath(wavPath);
            const int selectedChannel = channel->value();
            const auto audioModelFile = fsPath(audioModel);
            const auto textModelFile = fsPath(textModel);
            const auto tokenizerFile = fsPath(tokenizer);
            const double selectedTemperature = temperature->value();

            runner->start(
                [wavFile, selectedChannel, audioModelFile, textModelFile,
                 tokenizerFile, selectedTemperature,
                 candidates = std::move(candidates)]() -> QString {
                    if (candidates.empty()) {
                        throw std::runtime_error{
                            "Enter at least one candidate label"};
                    }
                    const auto wav = demo::loadWav(wavFile);
                    const auto mono = demo::selectMonoChannel(
                        wav.audio.view(),
                        static_cast<std::size_t>(selectedChannel));
                    const auto result = runClapClassification(
                        mono,
                        audioModelFile,
                        textModelFile,
                        tokenizerFile,
                        selectedTemperature,
                        candidates);

                    std::ostringstream text;
                    text << "input="
                         << wavSummary(wav).toStdString()
                         << "\n";
                    for (const auto& item : result.classes) {
                        text << item.label
                             << "="
                             << item.probability.value()
                             << "\n";
                    }
                    text << "\nNote: probabilities are normalized only "
                            "over the supplied candidate set.";
                    return QString::fromStdString(text.str());
                });
        });

    return page;
#else
    return unavailablePanel(
        "CLAP",
        "-DLIBAUDITION_WITH_CLAP",
        parent);
#endif
}

QWidget* createSherpaPanel(QWidget* parent) {
#if LIBAUDITION_DEMO_HAS_SHERPA
    auto* page = new QWidget{parent};
    auto* layout = new QVBoxLayout{page};

    layout->addWidget(description(
        "Real sherpa-onnx runners. This first workbench slice exposes "
        "Silero/TEN VAD, offline Whisper ASR and optional VITS/Piper "
        "TTS. Input conversion is explicit: the selected WAV channel "
        "must already use the backend's required sample rate."));

    auto* tabs = new QTabWidget{page};

    // VAD
    {
        auto* vadPage = new QWidget{tabs};
        auto* vadLayout = new QVBoxLayout{vadPage};
        auto* form = new QFormLayout;
        QLineEdit* wavPath = nullptr;
        QLineEdit* modelPath = nullptr;
        auto* channel = intBox(0, 255, 0);
        auto* modelType = new QComboBox;
        modelType->addItems({"Silero", "TEN"});
        auto* threshold =
            doubleBox(0.0, 1.0, 0.5, 3, 0.05);

        form->addRow(
            "WAV",
            pathEditor(
                wavPath,
                vadPage,
                false,
                "WAV audio (*.wav *.WAV);;All files (*)"));
        form->addRow("Channel", channel);
        form->addRow(
            "VAD model",
            pathEditor(
                modelPath,
                vadPage,
                false,
                "ONNX model (*.onnx);;All files (*)"));
        form->addRow("Model type", modelType);
        form->addRow("Threshold", threshold);
        vadLayout->addLayout(form);

        auto* run = new QPushButton{"Run VAD"};
        auto* output = outputBox();
        vadLayout->addWidget(run);
        vadLayout->addWidget(output);

        QObject::connect(
            run,
            &QPushButton::clicked,
            vadPage,
            [=]() {
                try {
                    const auto wav =
                        loadWavFrom(wavPath);
                    auto mono =
                        monoFrom(
                            wav,
                            channel->value());

                    audition::SherpaVadOptions options{};
                    if (modelType->currentIndex() == 0) {
                        auto& model =
                            std::get<
                                audition::SherpaSileroVadModel>(
                                options.model);
                        model.model =
                            fsPath(modelPath);
                        model.threshold =
                            static_cast<float>(
                                threshold->value());
                    } else {
                        audition::SherpaTenVadModel model{};
                        model.model =
                            fsPath(modelPath);
                        model.threshold =
                            static_cast<float>(
                                threshold->value());
                        options.model =
                            std::move(model);
                    }
                    options.sample_rate_hz =
                        mono.format().sample_rate_hz;

                    audition::SherpaVad vad{options};
                    const auto requirements =
                        vad.audioRequirements();
                    const std::size_t frameCount =
                        requirements.preferred_frame_count
                            .value_or(512U);
                    if (frameCount == 0U) {
                        throw std::runtime_error{
                            "VAD reported zero preferred frame count"};
                    }

                    auto session =
                        vad.createSession();

                    std::size_t activeFrames = 0U;
                    std::size_t processedFrames = 0U;
                    std::optional<audition::Probability>
                        lastProbability{};

                    const auto& source =
                        mono.samples();
                    for (std::size_t offset = 0U;
                         offset < source.size();
                         offset += frameCount) {
                        const std::size_t available =
                            std::min(
                                frameCount,
                                source.size() - offset);

                        std::vector<float> block(
                            frameCount,
                            0.0F);
                        std::copy_n(
                            source.data() + offset,
                            available,
                            block.data());

                        audition::AudioBuffer chunk{
                            std::move(block),
                            mono.format(),
                            audition::Timestamp{}};
                        const auto result =
                            session->process(
                                chunk.view());
                        ++processedFrames;
                        if (result.speech_active) {
                            ++activeFrames;
                        }
                        if (result.speech_probability
                                .has_value()) {
                            lastProbability =
                                result.speech_probability;
                        }
                    }

                    std::ostringstream text;
                    text << "input="
                         << wavSummary(wav).toStdString()
                         << "\npreferred_frame_count="
                         << frameCount
                         << "\nprocessed_blocks="
                         << processedFrames
                         << "\nspeech_active_blocks="
                         << activeFrames
                         << "\nlast_probability=";
                    if (lastProbability.has_value()) {
                        text << lastProbability->value();
                    } else {
                        text << "<backend does not expose calibrated probability>";
                    }
                    output->setPlainText(
                        QString::fromStdString(
                            text.str()));
                } catch (const std::exception& error) {
                    output->setPlainText(
                        exceptionText(error));
                }
            });

        tabs->addTab(vadPage, "VAD");
    }

    // Offline Whisper ASR
    {
        auto* asrPage = new QWidget{tabs};
        auto* asrLayout = new QVBoxLayout{asrPage};
        auto* form = new QFormLayout;
        QLineEdit* wavPath = nullptr;
        QLineEdit* encoder = nullptr;
        QLineEdit* decoder = nullptr;
        QLineEdit* tokens = nullptr;
        auto* channel = intBox(0, 255, 0);
        auto* language = new QLineEdit;
        language->setPlaceholderText(
            "empty = model/default");

        form->addRow(
            "WAV",
            pathEditor(
                wavPath,
                asrPage,
                false,
                "WAV audio (*.wav *.WAV);;All files (*)"));
        form->addRow("Channel", channel);
        form->addRow(
            "Whisper encoder",
            pathEditor(
                encoder,
                asrPage,
                false,
                "ONNX model (*.onnx);;All files (*)"));
        form->addRow(
            "Whisper decoder",
            pathEditor(
                decoder,
                asrPage,
                false,
                "ONNX model (*.onnx);;All files (*)"));
        form->addRow(
            "Tokens",
            pathEditor(
                tokens,
                asrPage,
                false,
                "Text files (*.txt);;All files (*)"));
        form->addRow("Language", language);
        asrLayout->addLayout(form);

        auto* run =
            new QPushButton{"Run offline Whisper ASR"};
        auto* output = outputBox();
        asrLayout->addWidget(run);
        asrLayout->addWidget(output);

        QObject::connect(
            run,
            &QPushButton::clicked,
            asrPage,
            [=]() {
                try {
                    const auto wav =
                        loadWavFrom(wavPath);
                    auto mono =
                        monoFrom(
                            wav,
                            channel->value());

                    audition::SherpaOfflineAsrOptions options{};
                    audition::SherpaOfflineWhisperModel model{};
                    model.encoder = fsPath(encoder);
                    model.decoder = fsPath(decoder);
                    model.language =
                        language->text().toStdString();
                    model.task = "transcribe";
                    model.enable_token_timestamps = true;
                    options.model = std::move(model);
                    options.tokens = fsPath(tokens);
                    options.features.sample_rate_hz =
                        mono.format().sample_rate_hz;

                    audition::SherpaOfflineAsr asr{
                        options};

                    audition::SpeechSegment segment{};
                    segment.segment_id =
                        audition::SpeechSegmentId{1U};
                    segment.audio =
                        std::move(mono);

                    const auto transcript =
                        asr.transcribe(segment);

                    std::ostringstream text;
                    text << "text="
                         << transcript.text
                         << "\nlanguage="
                         << transcript.language
                         << "\ntokens="
                         << transcript.tokens.size()
                         << "\nwords="
                         << transcript.words.size()
                         << "\nconfidence=";
                    if (transcript.confidence
                            .has_value()) {
                        text << transcript.confidence
                                    ->value();
                    } else {
                        text << "<absent>";
                    }
                    output->setPlainText(
                        QString::fromStdString(
                            text.str()));
                } catch (const std::exception& error) {
                    output->setPlainText(
                        exceptionText(error));
                }
            });

        tabs->addTab(asrPage, "Offline ASR");
    }

    addSherpaExtendedWorkbenchTabs(tabs);
    addSherpaSpeakerWorkbenchTabs(tabs);

#if LIBAUDITION_DEMO_HAS_SHERPA_TTS
    // VITS/Piper TTS
    {
        auto* ttsPage = new QWidget{tabs};
        auto* ttsLayout = new QVBoxLayout{ttsPage};
        auto* form = new QFormLayout;
        QLineEdit* model = nullptr;
        QLineEdit* tokens = nullptr;
        QLineEdit* dataDir = nullptr;
        QLineEdit* lexicon = nullptr;
        QLineEdit* outputPath = nullptr;
        auto* textInput =
            new QLineEdit{"Hello from libaudition."};
        auto* language =
            new QLineEdit{"en"};
        auto* speed =
            doubleBox(0.1, 5.0, 1.0, 2, 0.1);
        auto* speaker =
            intBox(0, 100000, 0);

        form->addRow(
            "VITS/Piper model",
            pathEditor(
                model,
                ttsPage,
                false,
                "ONNX model (*.onnx);;All files (*)"));
        form->addRow(
            "Tokens",
            pathEditor(
                tokens,
                ttsPage,
                false,
                "Text files (*.txt);;All files (*)"));
        form->addRow(
            "espeak-ng data",
            pathEditor(
                dataDir,
                ttsPage,
                true));
        form->addRow(
            "Lexicon (optional)",
            pathEditor(
                lexicon,
                ttsPage,
                false,
                "Text files (*.txt);;All files (*)"));
        form->addRow("Text", textInput);
        form->addRow("Language", language);
        form->addRow("Speed", speed);
        form->addRow("Speaker ID", speaker);

        auto* outputRow =
            new QWidget{ttsPage};
        auto* outputLayout =
            new QHBoxLayout{outputRow};
        outputLayout->setContentsMargins(
            0, 0, 0, 0);
        outputPath = new QLineEdit{
            "libaudition_tts.wav"};
        auto* chooseOutput =
            new QPushButton{"Save as…"};
        outputLayout->addWidget(
            outputPath,
            1);
        outputLayout->addWidget(
            chooseOutput);
        QObject::connect(
            chooseOutput,
            &QPushButton::clicked,
            outputRow,
            [outputPath]() {
                const auto path =
                    QFileDialog::getSaveFileName(
                        nullptr,
                        "Save synthesized WAV",
                        outputPath->text(),
                        "WAV audio (*.wav)");
                if (!path.isEmpty()) {
                    outputPath->setText(path);
                }
            });
        form->addRow("Output WAV", outputRow);

        ttsLayout->addLayout(form);
        auto* run =
            new QPushButton{"Synthesize with VITS/Piper"};
        auto* output = outputBox();
        ttsLayout->addWidget(run);
        ttsLayout->addWidget(output);

        QObject::connect(
            run,
            &QPushButton::clicked,
            ttsPage,
            [=]() {
                try {
                    audition::SherpaTtsOptions options{};
                    auto& vits =
                        std::get<
                            audition::SherpaTtsVitsModel>(
                            options.model);
                    vits.model =
                        fsPath(model);
                    vits.tokens =
                        fsPath(tokens);
                    vits.data_dir =
                        fsPath(dataDir);
                    if (!lexicon->text().isEmpty()) {
                        vits.lexicon =
                            fsPath(lexicon);
                    }
                    options.speaker_id =
                        speaker->value();
                    if (!language->text().isEmpty()) {
                        options.languages = {
                            language->text().toStdString()};
                    }

                    audition::SherpaTts tts{
                        options};

                    audition::SpeechSynthesisRequest request{};
                    request.text =
                        textInput->text().toStdString();
                    request.language =
                        language->text().toStdString();
                    request.speed =
                        speed->value();

                    const auto audio =
                        tts.synthesize(request);
                    demo::saveWavPcm16(
                        std::filesystem::path{
                            outputPath->text()
                                .toStdString()},
                        audio.view());

                    std::ostringstream text;
                    text << "backend="
                         << tts.backendInfo().name
                         << "\nsample_rate_hz="
                         << audio.format().sample_rate_hz
                         << "\nframes="
                         << audio.frameCount()
                         << "\nduration_s="
                         << audio.duration().seconds()
                         << "\nsaved="
                         << outputPath->text().toStdString();
                    output->setPlainText(
                        QString::fromStdString(
                            text.str()));
                } catch (const std::exception& error) {
                    output->setPlainText(
                        exceptionText(error));
                }
            });

        tabs->addTab(ttsPage, "VITS / Piper TTS");
    }
#else
    tabs->addTab(
        unavailablePanel(
            "Sherpa TTS",
            "-DLIBAUDITION_SHERPA_ENABLE_TTS",
            tabs),
        "TTS");
#endif

    layout->addWidget(tabs);
    return page;
#else
    return unavailablePanel(
        "sherpa-onnx",
        "-DLIBAUDITION_WITH_SHERPA",
        parent);
#endif
}

[[maybe_unused]] std::vector<std::size_t> parseChannelMap(
    const QString& text) {
    std::vector<std::size_t> result{};
    for (const auto& part :
         text.split(',', Qt::SkipEmptyParts)) {
        bool ok = false;
        const auto value =
            part.trimmed().toULongLong(&ok);
        if (!ok) {
            throw std::runtime_error{
                "Invalid ODAS channel mapping"};
        }
        result.push_back(
            static_cast<std::size_t>(value));
    }
    if (result.empty()) {
        throw std::runtime_error{
            "ODAS channel mapping is empty"};
    }
    return result;
}

[[maybe_unused]] std::vector<audition::MicrophoneGeometry>
parseGeometry(const QString& text) {
    std::vector<audition::MicrophoneGeometry> result{};
    for (const auto& line :
         text.split('\n', Qt::SkipEmptyParts)) {
        const auto parts =
            line.split(',', Qt::SkipEmptyParts);
        if (parts.size() != 3) {
            throw std::runtime_error{
                "Each microphone geometry line must be x,y,z"};
        }
        bool okX = false;
        bool okY = false;
        bool okZ = false;
        const double x =
            parts[0].trimmed().toDouble(&okX);
        const double y =
            parts[1].trimmed().toDouble(&okY);
        const double z =
            parts[2].trimmed().toDouble(&okZ);
        if (!okX || !okY || !okZ) {
            throw std::runtime_error{
                "Invalid microphone coordinate"};
        }
        result.push_back(
            audition::MicrophoneGeometry{{x, y, z}});
    }
    if (result.empty()) {
        throw std::runtime_error{
            "Microphone geometry is empty"};
    }
    return result;
}

QWidget* createOdasPanel(QWidget* parent) {
#if LIBAUDITION_DEMO_HAS_ODAS
    auto* page = new QWidget{parent};
    auto* layout = new QVBoxLayout{page};

    layout->addWidget(description(
        "Offline ODAS runner over a multichannel WAV. One configured "
        "hop is fed per call, exactly like a streaming application. "
        "Geometry and 0-based input-channel mapping are editable; the "
        "defaults are only an example four-microphone cross and may "
        "need changing for your recording."));

    auto* form = new QFormLayout;
    QLineEdit* wavPath = nullptr;
    auto* channelMap =
        new QLineEdit{"1,2,3,4"};
    auto* geometry = new QPlainTextEdit{
        "-0.032,0.000,0.000\n"
        "0.000,-0.032,0.000\n"
        "0.032,0.000,0.000\n"
        "0.000,0.032,0.000"};
    geometry->setMaximumHeight(110);
    auto* hop = intBox(32, 4096, 128);
    auto* frame = intBox(64, 8192, 256);
    auto* separation =
        new QCheckBox{"Enable separated audio"};
    separation->setChecked(true);

    form->addRow(
        "Multichannel WAV",
        pathEditor(
            wavPath,
            page,
            false,
            "WAV audio (*.wav *.WAV);;All files (*)"));
    form->addRow(
        "ODAS input channels (0-based)",
        channelMap);
    form->addRow(
        "Mic geometry x,y,z [m]",
        geometry);
    form->addRow("Hop size", hop);
    form->addRow("Frame size", frame);
    form->addRow(separation);
    layout->addLayout(form);

    auto* run =
        new QPushButton{"Run ODAS over WAV"};
    auto* output = outputBox();
    layout->addWidget(run);
    layout->addWidget(output);

    QObject::connect(
        run,
        &QPushButton::clicked,
        page,
        [=]() {
            try {
                const auto wav =
                    loadWavFrom(wavPath);
                const auto format =
                    wav.audio.format();

                audition::OdasOptions options{};
                options.microphone_array.microphones =
                    parseGeometry(
                        geometry->toPlainText());
                options.input_channels =
                    parseChannelMap(
                        channelMap->text());
                options.sample_rate_hz =
                    format.sample_rate_hz;
                options.hop_size =
                    static_cast<std::uint32_t>(
                        hop->value());
                options.frame_size =
                    static_cast<std::uint32_t>(
                        frame->value());
                options.sss.enabled =
                    separation->isChecked();

                audition::OdasSpatialEngine engine{
                    options};

                const std::size_t channelCount =
                    format.channel_count;
                const std::size_t hopFrames =
                    options.hop_size;
                const auto& samples =
                    wav.audio.samples();

                std::size_t processedHops = 0U;
                std::size_t hopsWithTracks = 0U;
                std::size_t maxTracks = 0U;
                std::map<
                    std::uint64_t,
                    audition::SpatialTrack>
                    lastTracks{};

                for (std::size_t frameOffset = 0U;
                     frameOffset + hopFrames <=
                         wav.audio.frameCount();
                     frameOffset += hopFrames) {
                    const std::size_t sampleOffset =
                        frameOffset * channelCount;
                    const std::size_t sampleCount =
                        hopFrames * channelCount;

                    std::vector<float> block(
                        samples.begin() +
                            static_cast<std::ptrdiff_t>(
                                sampleOffset),
                        samples.begin() +
                            static_cast<std::ptrdiff_t>(
                                sampleOffset + sampleCount));

                    const double seconds =
                        static_cast<double>(
                            frameOffset) /
                        static_cast<double>(
                            format.sample_rate_hz);
                    const auto nanoseconds =
                        static_cast<std::int64_t>(
                            seconds * 1.0e9);

                    audition::AudioBuffer chunk{
                        std::move(block),
                        format,
                        audition::Timestamp{
                            nanoseconds,
                            {
                                audition::ClockDomain::Monotonic,
                                1U}},
                        static_cast<std::uint64_t>(
                            processedHops)};

                    const auto result =
                        engine.process(
                            chunk.view());
                    ++processedHops;
                    if (!result.tracks.empty()) {
                        ++hopsWithTracks;
                    }
                    maxTracks =
                        std::max(
                            maxTracks,
                            result.tracks.size());
                    for (const auto& track :
                         result.tracks) {
                        lastTracks[
                            track.track_id.value()] =
                            track;
                    }
                }

                std::ostringstream text;
                text << "input="
                     << wavSummary(wav).toStdString()
                     << "\nbackend="
                     << engine.backendInfo().name
                     << "\nprocessed_hops="
                     << processedHops
                     << "\nhops_with_tracks="
                     << hopsWithTracks
                     << "\nmax_tracks_in_hop="
                     << maxTracks
                     << "\nunique_track_ids="
                     << lastTracks.size()
                     << "\n\n";

                for (const auto& [id, track] :
                     lastTracks) {
                    const auto v =
                        track.direction.direction.vector();
                    const double azimuth =
                        std::atan2(v.y, v.x) *
                        180.0 / 3.14159265358979323846;
                    const double elevation =
                        std::atan2(
                            v.z,
                            std::sqrt(
                                v.x * v.x +
                                v.y * v.y)) *
                        180.0 / 3.14159265358979323846;
                    text << "track="
                         << id
                         << " activity="
                         << track.activity.value()
                         << " azimuth_deg="
                         << azimuth
                         << " elevation_deg="
                         << elevation
                         << "\n";
                }

                output->setPlainText(
                    QString::fromStdString(text.str()));
            } catch (const std::exception& error) {
                output->setPlainText(
                    exceptionText(error));
            }
        });

    return page;
#else
    return unavailablePanel(
        "ODAS",
        "-DLIBAUDITION_WITH_ODAS",
        parent);
#endif
}

}  // namespace

QWidget* createBackendWorkbench(QWidget* parent) {
    auto* page = new QWidget{parent};
    auto* layout = new QVBoxLayout{page};

    layout->addWidget(description(
        "<h2>Real backend workbench</h2>"
        "<p>These panels call libaudition backend classes directly. "
        "No backend owns audio capture, no model is downloaded by the "
        "GUI, and sample-rate/channel conversions are never hidden.</p>"));

    auto* tabs = new QTabWidget{page};
    tabs->addTab(
        createWavInspector(tabs),
        "WAV");
    tabs->addTab(
        createWorldPanel(tabs),
        "WORLD");
    tabs->addTab(
        createAasistPanel(tabs),
        "AASIST");
    tabs->addTab(
        createClapPanel(tabs),
        "CLAP");
    tabs->addTab(
        createSherpaPanel(tabs),
        "Sherpa");
    tabs->addTab(
        createOdasPanel(tabs),
        "ODAS");
    addOptionalBackendWorkbenchTabs(tabs);

    layout->addWidget(tabs);
    return page;
}
