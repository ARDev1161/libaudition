#include "sherpa_extended_workbench.hpp"
#include "async_panel_runner.hpp"

#include "wav_io.hpp"

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

#if LIBAUDITION_DEMO_HAS_SHERPA
#include <audition/backends/sherpa.hpp>
#include <audition/interfaces/audio.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#endif

namespace {

#if LIBAUDITION_DEMO_HAS_SHERPA

QString exceptionText(const std::exception& error) {
    return QStringLiteral("error: ") + QString::fromUtf8(error.what());
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

QSpinBox* intBox(
    int minimum,
    int maximum,
    int value,
    int step = 1) {
    auto* box = new QSpinBox;
    box->setRange(minimum, maximum);
    box->setValue(value);
    box->setSingleStep(step);
    return box;
}

QDoubleSpinBox* doubleBox(
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

QWidget* pathEditor(
    QLineEdit*& edit,
    QWidget* parent,
    bool directory = false,
    const QString& filter = QStringLiteral("All files (*)")) {
    auto* row = new QWidget{parent};
    auto* layout = new QHBoxLayout{row};
    layout->setContentsMargins(0, 0, 0, 0);

    edit = new QLineEdit{row};
    auto* choose = new QPushButton{
        directory ? QStringLiteral("Choose…")
                  : QStringLiteral("Browse…"),
        row};

    layout->addWidget(edit, 1);
    layout->addWidget(choose);

    QObject::connect(
        choose,
        &QPushButton::clicked,
        row,
        [edit, directory, filter]() {
            QString path;
            if (directory) {
                path = QFileDialog::getExistingDirectory(
                    nullptr,
                    QStringLiteral("Choose directory"),
                    edit->text());
            } else {
                path = QFileDialog::getOpenFileName(
                    nullptr,
                    QStringLiteral("Choose file"),
                    edit->text(),
                    filter);
            }
            if (!path.isEmpty()) {
                edit->setText(path);
            }
        });

    return row;
}

std::filesystem::path fsPath(const QLineEdit* edit) {
    return std::filesystem::path{edit->text().toStdString()};
}

demo::LoadedWav loadWavFrom(const QLineEdit* edit) {
    if (edit->text().isEmpty()) {
        throw std::runtime_error{"WAV path is required"};
    }
    return demo::loadWav(fsPath(edit));
}

audition::AudioBuffer loadMono(
    const QLineEdit* wavPath,
    const QSpinBox* channel) {
    auto wav = loadWavFrom(wavPath);
    return demo::selectMonoChannel(
        wav.audio.view(),
        static_cast<std::size_t>(channel->value()));
}

QString wavSummary(const audition::AudioBuffer& audio) {
    std::ostringstream text;
    text << "sample_rate_hz=" << audio.format().sample_rate_hz
         << " channels=" << audio.format().channel_count
         << " frames=" << audio.frameCount()
         << " duration_s=" << audio.duration().seconds();
    return QString::fromStdString(text.str());
}

audition::AudioBuffer chunkOf(
    audition::AudioView audio,
    std::size_t firstFrame,
    std::size_t frameCount,
    std::uint64_t sequence) {
    if (audio.format().channel_count != 1U) {
        throw std::runtime_error{"Chunk helper requires mono audio"};
    }

    const std::size_t endFrame =
        std::min(firstFrame + frameCount, audio.frameCount());
    if (firstFrame >= endFrame) {
        return audition::AudioBuffer{
            {},
            audio.format(),
            audio.captureTime(),
            sequence};
    }

    std::vector<float> samples(
        audio.data() + static_cast<std::ptrdiff_t>(firstFrame),
        audio.data() + static_cast<std::ptrdiff_t>(endFrame));
    return audition::AudioBuffer{
        std::move(samples),
        audio.format(),
        audio.captureTime(),
        sequence};
}

audition::SherpaOnlineModel onlineModel(
    int kind,
    const QLineEdit* primary,
    const QLineEdit* decoder,
    const QLineEdit* joiner) {
    switch (kind) {
    case 0:
        return audition::SherpaOnlineTransducerModel{
            fsPath(primary),
            fsPath(decoder),
            fsPath(joiner)};
    case 1:
        return audition::SherpaOnlineParaformerModel{
            fsPath(primary),
            fsPath(decoder)};
    case 2:
        return audition::SherpaOnlineZipformer2CtcModel{
            fsPath(primary)};
    case 3:
        return audition::SherpaOnlineNemoCtcModel{
            fsPath(primary)};
    case 4:
        return audition::SherpaOnlineToneCtcModel{
            fsPath(primary)};
    default:
        throw std::runtime_error{"Unknown sherpa online model family"};
    }
}

QComboBox* onlineModelCombo(QWidget* parent) {
    auto* combo = new QComboBox{parent};
    combo->addItem("Transducer (encoder/decoder/joiner)");
    combo->addItem("Paraformer (encoder/decoder)");
    combo->addItem("Zipformer2 CTC (single model)");
    combo->addItem("NeMo CTC (single model)");
    combo->addItem("Tone CTC (single model)");
    return combo;
}

QWidget* createStreamingAsrPanel(QWidget* parent) {
    auto* page = new QWidget{parent};
    auto* layout = new QVBoxLayout{page};

    layout->addWidget(description(
        "Runs SherpaStreamingAsr over a WAV in explicit chunks. "
        "The feature sample rate is a visible model contract; the GUI "
        "does not resample the WAV. Primary model means encoder for "
        "Transducer/Paraformer and the single model file for CTC families."));

    auto* form = new QFormLayout;
    QLineEdit* wavPath = nullptr;
    QLineEdit* primary = nullptr;
    QLineEdit* decoder = nullptr;
    QLineEdit* joiner = nullptr;
    QLineEdit* tokens = nullptr;
    auto* channel = intBox(0, 255, 0);
    auto* modelKind = onlineModelCombo(page);
    auto* sampleRate = intBox(8000, 192000, 16000, 1000);
    auto* featureDim = intBox(1, 1024, 80);
    auto* chunkFrames = intBox(1, 1000000, 1600, 160);
    auto* endpoint = new QCheckBox{"Enable endpoint detection"};

    form->addRow(
        "Input WAV",
        pathEditor(
            wavPath,
            page,
            false,
            "WAV audio (*.wav *.WAV);;All files (*)"));
    form->addRow("Channel (0-based)", channel);
    form->addRow("Online model family", modelKind);
    form->addRow(
        "Primary model / encoder",
        pathEditor(primary, page, false, "ONNX model (*.onnx);;All files (*)"));
    form->addRow(
        "Decoder (if used)",
        pathEditor(decoder, page, false, "ONNX model (*.onnx);;All files (*)"));
    form->addRow(
        "Joiner (Transducer only)",
        pathEditor(joiner, page, false, "ONNX model (*.onnx);;All files (*)"));
    form->addRow(
        "Tokens",
        pathEditor(tokens, page, false, "Text files (*.txt);;All files (*)"));
    form->addRow("Feature sample rate", sampleRate);
    form->addRow("Feature dim", featureDim);
    form->addRow("Chunk frames", chunkFrames);
    form->addRow(endpoint);
    layout->addLayout(form);

    auto* run = new QPushButton{"Run streaming ASR"};
    auto* output = outputBox();
    layout->addWidget(run);
    layout->addWidget(output);

    auto* runner = new AsyncPanelRunner{page, layout, {run}, output};
    QObject::connect(
        run,
        &QPushButton::clicked,
        page,
        [=]() {
            // Capture widget values and complete model configuration on UI thread.
            const auto wavFile = fsPath(wavPath);
            const int selectedChannel = channel->value();
            const auto selectedBlock =
                static_cast<std::size_t>(chunkFrames->value());
            audition::SherpaStreamingAsrOptions options{};
            options.model = onlineModel(
                modelKind->currentIndex(),
                primary, decoder, joiner);
            options.tokens = fsPath(tokens);
            options.features.sample_rate_hz =
                static_cast<std::uint32_t>(sampleRate->value());
            options.features.feature_dim =
                static_cast<std::uint32_t>(featureDim->value());
            options.enable_endpoint = endpoint->isChecked();

            runner->start(
                [wavFile, selectedChannel, selectedBlock,
                 options = std::move(options)]() -> QString {
                    const auto wav = demo::loadWav(wavFile);
                    auto mono = demo::selectMonoChannel(
                        wav.audio.view(),
                        static_cast<std::size_t>(selectedChannel));

                audition::SherpaStreamingAsr engine{options};
                auto session = engine.createSession();

                std::size_t chunks = 0U;
                std::size_t partialChanges = 0U;
                bool endpointDetected = false;
                std::string lastPartial{};

                const std::size_t block = selectedBlock;
                for (std::size_t offset = 0U;
                     offset < mono.frameCount();
                     offset += block) {
                    auto chunk = chunkOf(
                        mono.view(),
                        offset,
                        block,
                        static_cast<std::uint64_t>(chunks));
                    session->accept(chunk.view());
                    ++chunks;

                    const auto partial = session->partial();
                    if (partial.text != lastPartial) {
                        lastPartial = partial.text;
                        ++partialChanges;
                    }
                    endpointDetected =
                        endpointDetected || session->endpointDetected();
                }

                const auto finalTranscript = session->finalize();

                std::ostringstream text;
                text << "backend=" << engine.backendInfo().name
                     << "\ninput=" << wavSummary(mono).toStdString()
                     << "\nchunks=" << chunks
                     << "\npartial_changes=" << partialChanges
                     << "\nendpoint_detected="
                     << (endpointDetected ? "yes" : "no")
                     << "\nfinal_text=" << finalTranscript.text
                     << "\nlanguage=" << finalTranscript.language
                     << "\ntokens=" << finalTranscript.tokens.size()
                     << "\nwords=" << finalTranscript.words.size();
                    return QString::fromStdString(text.str());
                });
        });

    return page;
}

QWidget* createKeywordSpotterPanel(QWidget* parent) {
    auto* page = new QWidget{parent};
    auto* layout = new QVBoxLayout{page};

    layout->addWidget(description(
        "Runs SherpaKeywordSpotter over a WAV in explicit chunks. "
        "Keywords can be entered inline; use the exact sherpa-onnx keyword "
        "syntax required by the selected model/tokenization."));

    auto* form = new QFormLayout;
    QLineEdit* wavPath = nullptr;
    QLineEdit* primary = nullptr;
    QLineEdit* decoder = nullptr;
    QLineEdit* joiner = nullptr;
    QLineEdit* tokens = nullptr;
    auto* channel = intBox(0, 255, 0);
    auto* modelKind = onlineModelCombo(page);
    auto* sampleRate = intBox(8000, 192000, 16000, 1000);
    auto* featureDim = intBox(1, 1024, 80);
    auto* chunkFrames = intBox(1, 1000000, 1600, 160);
    auto* keywords = new QLineEdit{"hello world"};
    auto* score = doubleBox(0.001, 100.0, 1.0, 3, 0.1);
    auto* threshold = doubleBox(0.0, 1.0, 0.25, 3, 0.05);
    auto* trailingBlanks = intBox(0, 1000, 1);

    form->addRow(
        "Input WAV",
        pathEditor(
            wavPath,
            page,
            false,
            "WAV audio (*.wav *.WAV);;All files (*)"));
    form->addRow("Channel (0-based)", channel);
    form->addRow("Online model family", modelKind);
    form->addRow(
        "Primary model / encoder",
        pathEditor(primary, page, false, "ONNX model (*.onnx);;All files (*)"));
    form->addRow(
        "Decoder (if used)",
        pathEditor(decoder, page, false, "ONNX model (*.onnx);;All files (*)"));
    form->addRow(
        "Joiner (Transducer only)",
        pathEditor(joiner, page, false, "ONNX model (*.onnx);;All files (*)"));
    form->addRow(
        "Tokens",
        pathEditor(tokens, page, false, "Text files (*.txt);;All files (*)"));
    form->addRow("Keywords", keywords);
    form->addRow("Feature sample rate", sampleRate);
    form->addRow("Feature dim", featureDim);
    form->addRow("Chunk frames", chunkFrames);
    form->addRow("Keywords score", score);
    form->addRow("Keywords threshold", threshold);
    form->addRow("Trailing blanks", trailingBlanks);
    layout->addLayout(form);

    auto* run = new QPushButton{"Run keyword spotting"};
    auto* output = outputBox();
    layout->addWidget(run);
    layout->addWidget(output);

    auto* runner = new AsyncPanelRunner{page, layout, {run}, output};
    QObject::connect(
        run,
        &QPushButton::clicked,
        page,
        [=]() {
            // Capture widget values and complete model configuration on UI thread.
            const auto wavFile = fsPath(wavPath);
            const int selectedChannel = channel->value();
            const auto selectedBlock =
                static_cast<std::size_t>(chunkFrames->value());
            audition::SherpaKeywordSpotterOptions options{};
            options.model = onlineModel(
                modelKind->currentIndex(),
                primary, decoder, joiner);
            options.tokens = fsPath(tokens);
            options.features.sample_rate_hz =
                static_cast<std::uint32_t>(sampleRate->value());
            options.features.feature_dim =
                static_cast<std::uint32_t>(featureDim->value());
            options.keywords = keywords->text().toStdString();
            options.keywords_score =
                static_cast<float>(score->value());
            options.keywords_threshold =
                static_cast<float>(threshold->value());
            options.num_trailing_blanks =
                trailingBlanks->value();

            runner->start(
                [wavFile, selectedChannel, selectedBlock,
                 options = std::move(options)]() -> QString {
                    const auto wav = demo::loadWav(wavFile);
                    auto mono = demo::selectMonoChannel(
                        wav.audio.view(),
                        static_cast<std::size_t>(selectedChannel));

                audition::SherpaKeywordSpotter spotter{options};
                auto session = spotter.createSession();

                std::vector<audition::KeywordHit> hits;
                std::size_t chunks = 0U;
                const std::size_t block = selectedBlock;

                for (std::size_t offset = 0U;
                     offset < mono.frameCount();
                     offset += block) {
                    auto chunk = chunkOf(
                        mono.view(),
                        offset,
                        block,
                        static_cast<std::uint64_t>(chunks));
                    auto chunkHits = session->process(chunk.view());
                    hits.insert(
                        hits.end(),
                        std::make_move_iterator(chunkHits.begin()),
                        std::make_move_iterator(chunkHits.end()));
                    ++chunks;
                }

                std::ostringstream text;
                text << "backend=" << spotter.backendInfo().name
                     << "\ninput=" << wavSummary(mono).toStdString()
                     << "\nchunks=" << chunks
                     << "\nhits=" << hits.size()
                     << "\n\n";
                for (const auto& hit : hits) {
                    text << hit.keyword
                         << " offset_s=" << hit.offset.seconds()
                         << " probability=";
                    if (hit.probability.has_value()) {
                        text << hit.probability->value();
                    } else {
                        text << "<absent>";
                    }
                    text << "\n";
                }
                    return QString::fromStdString(text.str());
                });
        });

    return page;
}

QWidget* createLanguageIdPanel(QWidget* parent) {
    auto* page = new QWidget{parent};
    auto* layout = new QVBoxLayout{page};

    layout->addWidget(description(
        "Runs SherpaLanguageIdentifier (Whisper spoken-language ID) on one "
        "selected WAV channel. No resampling or downmixing is hidden."));

    auto* form = new QFormLayout;
    QLineEdit* wavPath = nullptr;
    QLineEdit* encoder = nullptr;
    QLineEdit* decoder = nullptr;
    auto* channel = intBox(0, 255, 0);
    auto* sampleRate = intBox(8000, 192000, 16000, 1000);
    auto* tailPaddings = intBox(0, 100000, 0);

    form->addRow(
        "Input WAV",
        pathEditor(
            wavPath,
            page,
            false,
            "WAV audio (*.wav *.WAV);;All files (*)"));
    form->addRow("Channel (0-based)", channel);
    form->addRow(
        "Whisper encoder",
        pathEditor(encoder, page, false, "ONNX model (*.onnx);;All files (*)"));
    form->addRow(
        "Whisper decoder",
        pathEditor(decoder, page, false, "ONNX model (*.onnx);;All files (*)"));
    form->addRow("Sample rate", sampleRate);
    form->addRow("Tail paddings", tailPaddings);
    layout->addLayout(form);

    auto* run = new QPushButton{"Identify language"};
    auto* output = outputBox();
    layout->addWidget(run);
    layout->addWidget(output);

    QObject::connect(
        run,
        &QPushButton::clicked,
        page,
        [=]() {
            try {
                auto mono = loadMono(wavPath, channel);

                audition::SherpaLanguageIdOptions options{};
                options.encoder = fsPath(encoder);
                options.decoder = fsPath(decoder);
                options.sample_rate_hz =
                    static_cast<std::uint32_t>(sampleRate->value());
                options.tail_paddings = tailPaddings->value();

                audition::SherpaLanguageIdentifier identifier{options};
                const auto scores = identifier.identify(mono.view());

                std::ostringstream text;
                text << "backend=" << identifier.backendInfo().name
                     << "\ninput=" << wavSummary(mono).toStdString()
                     << "\nresults=" << scores.size()
                     << "\n\n";
                for (const auto& scoreValue : scores) {
                    text << scoreValue.language << " probability=";
                    if (scoreValue.probability.has_value()) {
                        text << scoreValue.probability->value();
                    } else {
                        text << "<absent>";
                    }
                    text << "\n";
                }
                output->setPlainText(QString::fromStdString(text.str()));
            } catch (const std::exception& error) {
                output->setPlainText(exceptionText(error));
            }
        });

    return page;
}

QWidget* createAudioTaggerPanel(QWidget* parent) {
    auto* page = new QWidget{parent};
    auto* layout = new QVBoxLayout{page};

    layout->addWidget(description(
        "Runs SherpaAudioTagger on mono 16 kHz audio. "
        "Choose Zipformer or CED, provide the matching labels file, and "
        "inspect the top-k probabilities returned by the backend."));

    auto* form = new QFormLayout;
    QLineEdit* wavPath = nullptr;
    QLineEdit* model = nullptr;
    QLineEdit* labels = nullptr;
    auto* channel = intBox(0, 255, 0);
    auto* family = new QComboBox{page};
    family->addItem("Zipformer");
    family->addItem("CED");
    auto* topK = intBox(1, 1000, 5);

    form->addRow(
        "Input WAV",
        pathEditor(
            wavPath,
            page,
            false,
            "WAV audio (*.wav *.WAV);;All files (*)"));
    form->addRow("Channel (0-based)", channel);
    form->addRow("Model family", family);
    form->addRow(
        "Model",
        pathEditor(model, page, false, "ONNX model (*.onnx);;All files (*)"));
    form->addRow(
        "Labels",
        pathEditor(labels, page, false, "Text files (*.txt);;All files (*)"));
    form->addRow("Top K", topK);
    layout->addLayout(form);

    auto* run = new QPushButton{"Run audio tagging"};
    auto* output = outputBox();
    layout->addWidget(run);
    layout->addWidget(output);

    QObject::connect(
        run,
        &QPushButton::clicked,
        page,
        [=]() {
            try {
                auto mono = loadMono(wavPath, channel);

                audition::SherpaAudioTaggingOptions options{};
                if (family->currentIndex() == 0) {
                    options.model =
                        audition::SherpaAudioTaggingZipformerModel{
                            fsPath(model)};
                } else {
                    options.model =
                        audition::SherpaAudioTaggingCedModel{
                            fsPath(model)};
                }
                options.labels = fsPath(labels);
                options.top_k = topK->value();

                audition::SherpaAudioTagger tagger{options};
                const auto result = tagger.classify(mono.view());

                std::ostringstream text;
                text << "backend=" << tagger.backendInfo().name
                     << "\ninput=" << wavSummary(mono).toStdString()
                     << "\nclasses=" << result.classes.size()
                     << "\n\n";
                for (const auto& classScore : result.classes) {
                    text << classScore.label
                         << " probability="
                         << classScore.probability.value()
                         << "\n";
                }
                output->setPlainText(QString::fromStdString(text.str()));
            } catch (const std::exception& error) {
                output->setPlainText(exceptionText(error));
            }
        });

    return page;
}

QWidget* createDenoiserPanel(QWidget* parent) {
    auto* page = new QWidget{parent};
    auto* layout = new QVBoxLayout{page};

    layout->addWidget(description(
        "Runs the public libaudition noise-suppressor session with GTCRN or "
        "DPDFNet. Offline mode feeds the full WAV once. Streaming mode reads "
        "preferred_frame_count from capabilities and feeds the WAV in that "
        "model-sized block. Output is saved as PCM16 WAV."));

    auto* form = new QFormLayout;
    QLineEdit* wavPath = nullptr;
    QLineEdit* model = nullptr;
    QLineEdit* outputPath = nullptr;
    auto* channel = intBox(0, 255, 0);
    auto* mode = new QComboBox{page};
    mode->addItem("Offline");
    mode->addItem("Streaming");
    auto* family = new QComboBox{page};
    family->addItem("GTCRN");
    family->addItem("DPDFNet");
    auto* attenuation = doubleBox(0.0, 200.0, 0.0, 2, 1.0);

    form->addRow(
        "Input WAV",
        pathEditor(
            wavPath,
            page,
            false,
            "WAV audio (*.wav *.WAV);;All files (*)"));
    form->addRow("Channel (0-based)", channel);
    form->addRow("Mode", mode);
    form->addRow("Model family", family);
    form->addRow(
        "Model",
        pathEditor(model, page, false, "ONNX model (*.onnx);;All files (*)"));
    form->addRow(
        "DPDF attenuation limit dB (offline)",
        attenuation);

    auto* outputRow = new QWidget{page};
    auto* outputLayout = new QHBoxLayout{outputRow};
    outputLayout->setContentsMargins(0, 0, 0, 0);
    outputPath = new QLineEdit{"libaudition_denoised.wav"};
    auto* chooseOutput = new QPushButton{"Save as…"};
    outputLayout->addWidget(outputPath, 1);
    outputLayout->addWidget(chooseOutput);
    QObject::connect(
        chooseOutput,
        &QPushButton::clicked,
        outputRow,
        [outputPath]() {
            const auto path = QFileDialog::getSaveFileName(
                nullptr,
                "Save denoised WAV",
                outputPath->text(),
                "WAV audio (*.wav)");
            if (!path.isEmpty()) {
                outputPath->setText(path);
            }
        });
    form->addRow("Output WAV", outputRow);

    layout->addLayout(form);

    auto* run = new QPushButton{"Run denoiser"};
    auto* output = outputBox();
    layout->addWidget(run);
    layout->addWidget(output);

    QObject::connect(
        run,
        &QPushButton::clicked,
        page,
        [=]() {
            try {
                auto mono = loadMono(wavPath, channel);

                audition::SherpaSpeechDenoiserOptions options{};
                if (family->currentIndex() == 0) {
                    options.model =
                        audition::SherpaSpeechDenoiserGtcrnModel{
                            fsPath(model)};
                } else {
                    audition::SherpaSpeechDenoiserDpdfNetModel dpdf{
                        fsPath(model),
                        0.0F};
                    if (mode->currentIndex() == 0) {
                        dpdf.attenuation_limit_db =
                            static_cast<float>(attenuation->value());
                    }
                    options.model = dpdf;
                }

                std::unique_ptr<audition::INoiseSuppressor> denoiser;
                if (mode->currentIndex() == 0) {
                    denoiser =
                        std::make_unique<
                            audition::SherpaOfflineSpeechDenoiser>(
                            options);
                } else {
                    denoiser =
                        std::make_unique<
                            audition::SherpaStreamingSpeechDenoiser>(
                            options);
                }

                const auto capabilities = denoiser->capabilities();
                auto session = denoiser->createSession(mono.format());

                std::vector<float> samples;
                std::size_t chunks = 0U;
                std::size_t block = mono.frameCount();
                if (capabilities.streaming) {
                    block = capabilities.audio.preferred_frame_count
                                .value_or(512U);
                }
                if (block == 0U) {
                    throw std::runtime_error{
                        "Denoiser returned preferred_frame_count=0"};
                }

                for (std::size_t offset = 0U;
                     offset < mono.frameCount();
                     offset += block) {
                    auto chunk = chunkOf(
                        mono.view(),
                        offset,
                        block,
                        static_cast<std::uint64_t>(chunks));
                    auto processed = session->process(chunk.view());
                    const auto& part = processed.samples();
                    samples.insert(samples.end(), part.begin(), part.end());
                    ++chunks;
                }

                auto tail = session->flush();
                const auto& tailSamples = tail.samples();
                samples.insert(
                    samples.end(),
                    tailSamples.begin(),
                    tailSamples.end());

                audition::AudioBuffer denoised{
                    std::move(samples),
                    mono.format(),
                    mono.captureTime(),
                    mono.sequenceNumber()};
                demo::saveWavPcm16(
                    std::filesystem::path{
                        outputPath->text().toStdString()},
                    denoised.view());

                std::ostringstream text;
                text << "backend=" << denoiser->backendInfo().name
                     << "\nmode="
                     << (capabilities.streaming ? "streaming" : "offline")
                     << "\ninput=" << wavSummary(mono).toStdString()
                     << "\npreferred_frame_count=";
                if (capabilities.audio.preferred_frame_count.has_value()) {
                    text << *capabilities.audio.preferred_frame_count;
                } else {
                    text << "<absent>";
                }
                text << "\nchunks=" << chunks
                     << "\noutput_frames=" << denoised.frameCount()
                     << "\noutput_duration_s=" << denoised.duration().seconds()
                     << "\nsaved=" << outputPath->text().toStdString();
                output->setPlainText(QString::fromStdString(text.str()));
            } catch (const std::exception& error) {
                output->setPlainText(exceptionText(error));
            }
        });

    return page;
}

#endif

}  // namespace

void addSherpaExtendedWorkbenchTabs(QTabWidget* tabs) {
#if LIBAUDITION_DEMO_HAS_SHERPA
    tabs->addTab(createStreamingAsrPanel(tabs), "Streaming ASR");
    tabs->addTab(createKeywordSpotterPanel(tabs), "KWS");
    tabs->addTab(createLanguageIdPanel(tabs), "Language ID");
    tabs->addTab(createAudioTaggerPanel(tabs), "Audio tagging");
    tabs->addTab(createDenoiserPanel(tabs), "Denoise");
#else
    static_cast<void>(tabs);
#endif
}
