#include "demo_window.hpp"

#include "backend_workbench.hpp"
#include "wav_io.hpp"

#include <audition/audition.hpp>

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

audition::Timestamp ts(std::int64_t nanoseconds) {
    return audition::Timestamp{
        nanoseconds,
        {audition::ClockDomain::Monotonic, 0U}};
}

QDoubleSpinBox* makeDouble(
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

QSpinBox* makeInt(int minimum, int maximum, int value) {
    auto* box = new QSpinBox;
    box->setRange(minimum, maximum);
    box->setValue(value);
    return box;
}

QPlainTextEdit* makeOutput() {
    auto* output = new QPlainTextEdit;
    output->setReadOnly(true);
    output->setMinimumHeight(180);
    return output;
}

QLabel* makeDescription(const QString& text) {
    auto* label = new QLabel{text};
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

QString errorText(const std::exception& error) {
    return QStringLiteral("error: ") +
           QString::fromUtf8(error.what());
}

audition::SourceFingerprint fingerprint(float x, float y) {
    audition::SourceFingerprint result{};
    result.model_id = "qt-demo-fingerprint-v1";
    result.embedding = {x, y};
    result.quality = audition::Probability::one();
    return result;
}

audition::SourceIdentityObservation identityObservation(
    std::uint64_t track_id,
    std::int64_t timestamp_ns,
    audition::SourceFingerprint source_fingerprint) {
    audition::SourceIdentityObservation result{};
    result.track_id = audition::SpatialTrackId{track_id};
    result.timestamp = ts(timestamp_ns);
    result.direction.direction =
        audition::Direction3D::fromVector({1.0, 0.0, 0.0});
    result.fingerprint = std::move(source_fingerprint);
    return result;
}

class NoopFusion final : public audition::ISpatialFusion {
public:
    audition::BackendInfo backendInfo() const override {
        return {"qt-demo-noop-fusion", "1"};
    }

    std::optional<audition::PositionEstimate> fuse(
        const audition::SpatialFusionInput&) const override {
        return std::nullopt;
    }
};

QString methodName(audition::RangeEstimate::Method method) {
    switch (method) {
    case audition::RangeEstimate::Method::Unknown:
        return "Unknown";
    case audition::RangeEstimate::Method::LevelPrior:
        return "LevelPrior";
    case audition::RangeEstimate::Method::BearingTriangulation:
        return "BearingTriangulation";
    case audition::RangeEstimate::Method::Fused:
        return "Fused";
    }
    return "Unknown";
}


QWidget* requireTab(QTabWidget* tabs, const char* name) {
    if (tabs == nullptr) {
        throw std::runtime_error{"Qt self-test: missing tab widget"};
    }
    const auto title = QString::fromLatin1(name);
    for (int index = 0; index < tabs->count(); ++index) {
        if (tabs->tabText(index) == title) {
            return tabs->widget(index);
        }
    }
    throw std::runtime_error{
        "Qt self-test: missing tab '" + title.toStdString() + "'"};
}

void requireAction(QWidget* page, const char* action) {
    const auto expected = QString::fromLatin1(action);
    const auto buttons = page->findChildren<QPushButton*>();
    const auto found = std::any_of(
        buttons.begin(),
        buttons.end(),
        [&](const QPushButton* button) {
            return button->text() == expected && button->isEnabled();
        });
    if (!found) {
        throw std::runtime_error{
            "Qt self-test: missing enabled action '" +
            expected.toStdString() + "'"};
    }
}

void verifyWorkbench(QWidget* root) {
    auto* appTabs = qobject_cast<QTabWidget*>(root);
    if (appTabs == nullptr || appTabs->count() != 9) {
        throw std::runtime_error{"Qt self-test: unexpected application tab count"};
    }
    for (const char* name : {
             "Overview",
             "Audio / DSP",
             "SPL / Range",
             "Smoothing",
             "Source identity",
             "AcousticEvent",
             "Pipeline",
             "Backend workbench",
             "Backends"}) {
        static_cast<void>(requireTab(appTabs, name));
    }

    auto* backendPage = requireTab(appTabs, "Backend workbench");
    auto* backends = backendPage->findChild<QTabWidget*>();
    if (backends == nullptr || backends->count() != 9) {
        throw std::runtime_error{"Qt self-test: unexpected backend tab count"};
    }
    for (const char* name : {
             "WAV", "WORLD", "AASIST", "CLAP", "Sherpa",
             "ODAS", "libsamplerate", "AEC3", "GTSAM"}) {
        static_cast<void>(requireTab(backends, name));
    }

#if LIBAUDITION_DEMO_HAS_SHERPA
    auto* sherpa = requireTab(backends, "Sherpa")->findChild<QTabWidget*>();
    if (sherpa == nullptr) {
        throw std::runtime_error{"Qt self-test: Sherpa runner tabs missing"};
    }
    struct Runner {
        const char* tab;
        const char* button;
    };
    const Runner sherpaRunners[] = {
        {"VAD", "Run VAD"},
        {"Offline ASR", "Run offline Whisper ASR"},
        {"Streaming ASR", "Run streaming ASR"},
        {"KWS", "Run keyword spotting"},
        {"Language ID", "Identify language"},
        {"Audio tagging", "Run audio tagging"},
        {"Denoise", "Run denoiser"},
        {"Speaker embedding", "Extract speaker embedding"},
        {"Speaker verification", "Verify speaker"},
        {"Speaker identification", "Build index and identify"},
        {"Diarization", "Run speaker diarization"},
    };
    const int expectedCount =
        static_cast<int>(sizeof(sherpaRunners) / sizeof(sherpaRunners[0])) + 1;
    if (sherpa->count() != expectedCount) {
        throw std::runtime_error{"Qt self-test: unexpected Sherpa runner count"};
    }
    for (const auto& runner : sherpaRunners) {
        requireAction(requireTab(sherpa, runner.tab), runner.button);
    }
#if LIBAUDITION_DEMO_HAS_SHERPA_TTS
    requireAction(
        requireTab(sherpa, "VITS / Piper TTS"),
        "Synthesize with VITS/Piper");
#else
    static_cast<void>(requireTab(sherpa, "TTS"));
#endif
#endif

#if LIBAUDITION_DEMO_HAS_SAMPLERATE
    requireAction(requireTab(backends, "libsamplerate"), "Resample WAV");
#endif
#if LIBAUDITION_DEMO_HAS_AEC3
    requireAction(requireTab(backends, "AEC3"), "Run AEC3");
#endif
#if LIBAUDITION_DEMO_HAS_GTSAM
    requireAction(requireTab(backends, "GTSAM"), "Fuse observations");
#endif
}

void verifyWavRoundTrip() {
    QTemporaryDir directory;
    if (!directory.isValid()) {
        throw std::runtime_error{"Qt self-test: failed to create temporary directory"};
    }
    const std::filesystem::path path{
        directory.filePath("roundtrip.wav").toStdString()};

    audition::AudioBuffer input{
        {-1.0F, 0.0F, 0.5F, -0.5F, 0.25F, -0.25F, 0.0F, 1.0F},
        {48000U, 2U, audition::AudioLayout::Interleaved},
        ts(0)};
    demo::saveWavPcm16(path, input.view());
    const auto loaded = demo::loadWav(path);
    if (loaded.info.format_tag != 1U ||
        loaded.info.bits_per_sample != 16U ||
        loaded.audio.format().sample_rate_hz != 48000U ||
        loaded.audio.format().channel_count != 2U ||
        loaded.audio.frameCount() != 4U) {
        throw std::runtime_error{"Qt self-test: WAV format round-trip"};
    }
    const auto mono = demo::selectMonoChannel(loaded.audio.view(), 1U);
    const float expected[] = {0.0F, -0.5F, -0.25F, 1.0F};
    if (mono.frameCount() != 4U || mono.format().channel_count != 1U) {
        throw std::runtime_error{"Qt self-test: WAV channel selection"};
    }
    for (std::size_t index = 0U; index < 4U; ++index) {
        if (std::abs(mono.samples()[index] - expected[index]) > 0.00004F) {
            throw std::runtime_error{"Qt self-test: WAV sample mismatch"};
        }
    }
}

}  // namespace

DemoWindow::DemoWindow(QWidget* parent)
    : QMainWindow{parent} {
    setWindowTitle(
        QStringLiteral("libaudition %1 — interactive demo")
            .arg(QString::fromUtf8(audition::kVersion.data(),
                                   static_cast<int>(audition::kVersion.size()))));
    resize(1180, 780);

    auto* tabs = new QTabWidget;
    tabs->addTab(createOverviewTab(), "Overview");
    tabs->addTab(createAudioDspTab(), "Audio / DSP");
    tabs->addTab(createSpatialTab(), "SPL / Range");
    tabs->addTab(createSmoothingTab(), "Smoothing");
    tabs->addTab(createIdentityTab(), "Source identity");
    tabs->addTab(createEventsTab(), "AcousticEvent");
    tabs->addTab(createPipelineTab(), "Pipeline");
    tabs->addTab(createBackendWorkbench(tabs), "Backend workbench");
    tabs->addTab(createCapabilitiesTab(), "Backends");
    setCentralWidget(tabs);
}

QWidget* DemoWindow::createOverviewTab() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout{page};

    layout->addWidget(makeDescription(
        "<h2>libaudition interactive demo</h2>"
        "<p>This application is a playground for the acoustic-domain pieces of "
        "libaudition. Dependency-free algorithms run directly in the GUI; "
        "optional model/engine backends are listed on the <b>Backends</b> tab "
        "with their build status and required assets.</p>"));

    auto* table = new QTableWidget{0, 3};
    table->setHorizontalHeaderLabels(
        {"Area", "What libaudition provides", "Where to play"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);

    struct Row {
        const char* area;
        const char* description;
        const char* tab;
    };

    const Row rows[] = {
        {"Audio frontend / DSP",
         "routing, gain/DC/delay calibration, RMS/peak/DC/crest/clipping, explicit SNR",
         "Audio / DSP"},
        {"Spatial audio",
         "ODAS contracts, bearing/range/position observations, GTSAM acoustic fusion",
         "SPL / Range + Backends"},
        {"SPL / range",
         "dBFS->dB SPL calibration, source-level priors, uncertainty propagation",
         "SPL / Range"},
        {"Temporal geometry",
         "range/position moment smoothing with explicit process uncertainty",
         "Smoothing"},
        {"Persistent source identity",
         "SpatialTrackId -> persistent AcousticSourceId re-identification",
         "Source identity"},
        {"Acoustic events",
         "transactional begin/update/finish lifecycle with identity consistency",
         "AcousticEvent"},
        {"Speech",
         "VAD, ASR, KWS, language ID, denoising and TTS through Sherpa adapters",
         "Backends"},
        {"Speaker",
         "embeddings, verification, transient identification and diarization",
         "Backends"},
        {"Classification / embeddings",
         "audio tagging, CLAP embeddings and open-vocabulary classification",
         "Backends"},
        {"Voice analysis",
         "WORLD F0/traits and frame-level acoustic features",
         "Backends"},
        {"Authenticity",
         "AASIST bona-fide/spoof scoring with optional calibration",
         "Backends"},
        {"Infrastructure",
         "factory/model registries, bounded queues, cancellation and logging contracts",
         "Overview"},
    };

    for (const auto& row : rows) {
        const int index = table->rowCount();
        table->insertRow(index);
        table->setItem(index, 0, new QTableWidgetItem{row.area});
        table->setItem(index, 1, new QTableWidgetItem{row.description});
        table->setItem(index, 2, new QTableWidgetItem{row.tab});
    }

    layout->addWidget(table);
    layout->addWidget(makeDescription(
        "<b>Project boundary:</b> libaudition ends at acoustic-domain "
        "observations, identities and events. ROS 2, TF, robot state, cameras, "
        "radar, navigation and cross-modal fusion belong in a consuming "
        "application such as audio_nav2."));
    return page;
}

QWidget* DemoWindow::createAudioDspTab() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout{page};

    layout->addWidget(makeDescription(
        "Generate a mono sine wave, inspect measured signal quality, then pass "
        "it through AudioFrontend gain/DC/delay calibration. No hidden "
        "normalization is performed."));

    auto* form = new QFormLayout;
    auto* amplitude = makeDouble(0.0, 1.5, 0.5);
    auto* dc = makeDouble(-0.5, 0.5, 0.05);
    auto* frequency = makeDouble(20.0, 7000.0, 1000.0, 1, 10.0);
    auto* frames = makeInt(16, 160000, 16000);
    auto* gain = makeDouble(0.0, 4.0, 1.0);
    auto* dcCorrection = makeDouble(-0.5, 0.5, 0.05);
    auto* delay = makeDouble(0.0, 10.0, 0.0);
    auto* noiseRms = makeDouble(0.000001, 1.0, 0.01, 6, 0.001);

    form->addRow("Sine amplitude", amplitude);
    form->addRow("Injected DC offset", dc);
    form->addRow("Frequency [Hz]", frequency);
    form->addRow("Frames @ 16 kHz", frames);
    form->addRow("Frontend gain", gain);
    form->addRow("Frontend DC correction", dcCorrection);
    form->addRow("Frontend delay [samples]", delay);
    form->addRow("Explicit noise RMS for SNR", noiseRms);
    layout->addLayout(form);

    auto* run = new QPushButton{"Analyze synthetic audio"};
    auto* output = makeOutput();
    layout->addWidget(run);
    layout->addWidget(output);

    connect(run, &QPushButton::clicked, page, [=]() {
        try {
            std::vector<float> samples;
            samples.reserve(static_cast<std::size_t>(frames->value()));
            constexpr double sampleRate = 16000.0;
            for (int index = 0; index < frames->value(); ++index) {
                const double phase =
                    2.0 * kPi * frequency->value() *
                    static_cast<double>(index) / sampleRate;
                samples.push_back(static_cast<float>(
                    amplitude->value() * std::sin(phase) + dc->value()));
            }

            audition::AudioBuffer input{
                std::move(samples),
                {16000U, 1U, audition::AudioLayout::Interleaved},
                ts(0)};

            const auto raw =
                audition::dsp::analyzeQuality(input.view(), 0.999);

            audition::dsp::AudioFrontendConfig config{};
            audition::dsp::ChannelRoute route{};
            route.input_channel = 0U;
            route.calibration.gain = gain->value();
            route.calibration.dc_offset = dcCorrection->value();
            route.calibration.delay_samples = delay->value();
            config.routes.push_back(route);

            audition::dsp::AudioFrontend frontend{config};
            const auto processed = frontend.process(input.view());
            const auto after =
                audition::dsp::analyzeQuality(processed.view(), 0.999);

            std::ostringstream text;
            text << std::fixed << std::setprecision(6)
                 << "raw.rms=" << raw.aggregate.rms_linear << "\n"
                 << "raw.rms_dbfs=" << raw.aggregate.rms_dbfs << "\n"
                 << "raw.peak=" << raw.aggregate.peak_linear << "\n"
                 << "raw.dc=" << raw.aggregate.dc_offset << "\n"
                 << "raw.crest_db=" << raw.aggregate.crest_factor_db << "\n"
                 << "raw.clipping_ratio=" << raw.aggregate.clipping_ratio << "\n\n"
                 << "processed.rms=" << after.aggregate.rms_linear << "\n"
                 << "processed.rms_dbfs=" << after.aggregate.rms_dbfs << "\n"
                 << "processed.peak=" << after.aggregate.peak_linear << "\n"
                 << "processed.dc=" << after.aggregate.dc_offset << "\n"
                 << "processed.crest_db=" << after.aggregate.crest_factor_db << "\n"
                 << "processed.clipping_ratio=" << after.aggregate.clipping_ratio << "\n"
                 << "snr_db(explicit_noise)="
                 << audition::dsp::snrDb(
                        after.aggregate.rms_linear,
                        noiseRms->value());
            output->setPlainText(QString::fromStdString(text.str()));
        } catch (const std::exception& error) {
            output->setPlainText(errorText(error));
        }
    });

    return page;
}

QWidget* DemoWindow::createSpatialTab() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout{page};

    layout->addWidget(makeDescription(
        "Calibrate a measured digital level into dB SPL and derive a weak "
        "acoustic range prior from a source-level hypothesis. The underlying "
        "distance distribution is log-normal; libaudition exposes exact "
        "moment-matched mean/variance through the current range contract."));

    auto* form = new QFormLayout;
    auto* measuredDbfs = makeDouble(-120.0, 20.0, -40.0);
    auto* referenceDbfs = makeDouble(-120.0, 20.0, -20.0);
    auto* referenceSpl = makeDouble(20.0, 160.0, 94.0);
    auto* sourceSpl = makeDouble(20.0, 160.0, 80.0);
    auto* sourceVariance = makeDouble(0.0, 400.0, 4.0);
    auto* propagationVariance = makeDouble(0.0, 400.0, 4.0);
    auto* exponent = makeDouble(0.1, 10.0, 2.0);

    form->addRow("Measured [dBFS]", measuredDbfs);
    form->addRow("Calibration reference [dBFS]", referenceDbfs);
    form->addRow("Calibration reference [dB SPL]", referenceSpl);
    form->addRow("Source level @ 1 m [dB SPL]", sourceSpl);
    form->addRow("Source variance [dB²]", sourceVariance);
    form->addRow("Propagation variance [dB²]", propagationVariance);
    form->addRow("Path-loss exponent n", exponent);
    layout->addLayout(form);

    auto* run = new QPushButton{"Calibrate and estimate range"};
    auto* output = makeOutput();
    layout->addWidget(run);
    layout->addWidget(output);

    connect(run, &QPushButton::clicked, page, [=]() {
        try {
            audition::SoundPressureCalibrationProfile profile{};
            profile.profile_id = "qt-demo";
            profile.signal_path_id = "qt-demo-mic";
            profile.reference_level_db_spl = {referenceSpl->value(), 0.25};
            profile.measured_level_dbfs = {referenceDbfs->value(), 0.25};
            profile.reference_frequency_hz = 1000.0;
            profile.weighting = audition::SoundLevelWeighting::Z;
            profile.transfer_variance_db2 = 0.25;

            audition::SoundPressureLevelCalibrator calibrator{profile};

            audition::DbfsLevelObservation dbfs{};
            dbfs.timestamp = ts(0);
            dbfs.level_dbfs = measuredDbfs->value();
            dbfs.variance_db2 = 0.25;
            dbfs.weighting = audition::SoundLevelWeighting::Z;
            dbfs.signal_path_id = "qt-demo-mic";

            const auto spl = calibrator.calibrate(dbfs);
            if (!spl.has_value()) {
                output->setPlainText(
                    "No finite SPL observation (for example exact zero RMS).");
                return;
            }

            audition::SourceLevelPrior priors[] = {
                {
                    "demo-source",
                    {sourceSpl->value(), sourceVariance->value()},
                    1.0,
                    1.0,
                    audition::SoundLevelWeighting::Z,
                },
            };

            audition::RangeEstimationInput input{};
            input.sound_level = *spl;
            input.source_level_priors = priors;

            audition::SoundLevelRangePriorOptions options{};
            options.path_loss_exponent = exponent->value();
            options.propagation_variance_db2 =
                propagationVariance->value();
            audition::SoundLevelRangePriorEstimator estimator{options};

            const auto range = estimator.estimate(input);
            if (!range.has_value()) {
                output->setPlainText("No range prior available.");
                return;
            }

            std::ostringstream text;
            text << std::fixed << std::setprecision(4)
                 << "calibration.offset_db=" << calibrator.offsetDb() << "\n"
                 << "spl.mean_db=" << spl->level_db_spl.mean << "\n"
                 << "spl.variance_db2=" << spl->level_db_spl.variance << "\n"
                 << "range.mean_m=" << range->distance_m.mean << "\n"
                 << "range.variance_m2=" << range->distance_m.variance << "\n"
                 << "range.sigma_m=" << std::sqrt(range->distance_m.variance) << "\n"
                 << "range.method="
                 << methodName(range->method).toStdString() << "\n"
                 << "range.confidence=" << range->confidence.value()
                 << "  (zero by design: uncertainty is in variance)";
            output->setPlainText(QString::fromStdString(text.str()));
        } catch (const std::exception& error) {
            output->setPlainText(errorText(error));
        }
    });

    return page;
}

QWidget* DemoWindow::createSmoothingTab() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout{page};

    layout->addWidget(makeDescription(
        "Enter comma-separated acoustic range estimates. Temporal smoothing "
        "uses conservative mixture moments rather than treating adjacent "
        "measurements as independent."));

    auto* form = new QFormLayout;
    auto* sequence = new QLineEdit{"2.0, 2.4, 1.8, 2.2, 2.1"};
    auto* weight = makeDouble(0.0, 1.0, 0.35);
    auto* measurementVariance = makeDouble(0.0, 100.0, 0.04);
    auto* processVariance = makeDouble(0.0, 100.0, 0.01);

    form->addRow("Range sequence [m]", sequence);
    form->addRow("Current measurement weight", weight);
    form->addRow("Measurement variance [m²]", measurementVariance);
    form->addRow("Process variance [m²/s]", processVariance);
    layout->addLayout(form);

    auto* run = new QPushButton{"Run smoother"};
    auto* output = makeOutput();
    layout->addWidget(run);
    layout->addWidget(output);

    connect(run, &QPushButton::clicked, page, [=]() {
        try {
            audition::TemporalSpatialSmootherOptions options{};
            options.range_measurement_weight = weight->value();
            options.range_process_variance_m2_per_s =
                processVariance->value();
            audition::TemporalSpatialTrackSmoother smoother{options};

            const auto parts =
                sequence->text().split(',', Qt::SkipEmptyParts);
            if (parts.isEmpty()) {
                output->setPlainText("Enter at least one range value.");
                return;
            }

            std::ostringstream text;
            text << std::fixed << std::setprecision(4);

            for (int index = 0; index < parts.size(); ++index) {
                bool ok = false;
                const double raw = parts[index].trimmed().toDouble(&ok);
                if (!ok) {
                    output->setPlainText(
                        QStringLiteral("Invalid value: %1")
                            .arg(parts[index]));
                    return;
                }

                audition::SpatialTrack track{};
                track.track_id = audition::SpatialTrackId{1U};
                track.first_seen = ts(0);
                track.last_seen = ts(
                    static_cast<std::int64_t>(index) *
                    1'000'000'000LL);
                track.range = audition::RangeEstimate{
                    {raw, measurementVariance->value()},
                    audition::Probability::zero(),
                    audition::RangeEstimate::Method::LevelPrior};

                smoother.update(track);
                text << "t=" << index
                     << "  raw=" << raw
                     << "  smoothed=" << track.range->distance_m.mean
                     << "  variance=" << track.range->distance_m.variance
                     << "  method="
                     << methodName(track.range->method).toStdString()
                     << "\n";
            }

            output->setPlainText(QString::fromStdString(text.str()));
        } catch (const std::exception& error) {
            output->setPlainText(errorText(error));
        }
    });

    return page;
}

QWidget* DemoWindow::createIdentityTab() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout{page};

    layout->addWidget(makeDescription(
        "Simulate tracker handoff. The first transient SpatialTrackId is ended, "
        "then a second track arrives with another acoustic fingerprint. Change "
        "the embedding and resolver thresholds to see when the persistent "
        "AcousticSourceId is reacquired."));

    auto* form = new QFormLayout;
    auto* firstX = makeDouble(-1.0, 1.0, 1.0);
    auto* firstY = makeDouble(-1.0, 1.0, 0.0);
    auto* secondX = makeDouble(-1.0, 1.0, 0.999);
    auto* secondY = makeDouble(-1.0, 1.0, 0.01);
    auto* minSimilarity = makeDouble(-1.0, 1.0, 0.80);
    auto* associationThreshold = makeDouble(0.0, 1.0, 0.75);

    form->addRow("First fingerprint X", firstX);
    form->addRow("First fingerprint Y", firstY);
    form->addRow("Second fingerprint X", secondX);
    form->addRow("Second fingerprint Y", secondY);
    form->addRow("Minimum cosine similarity", minSimilarity);
    form->addRow("Association threshold", associationThreshold);
    layout->addLayout(form);

    auto* run = new QPushButton{"Simulate handoff / re-ID"};
    auto* output = makeOutput();
    layout->addWidget(run);
    layout->addWidget(output);

    connect(run, &QPushButton::clicked, page, [=]() {
        try {
            audition::InMemoryAcousticSourceRegistry registry;
            audition::HeuristicSourceIdentityResolverOptions options{};
            options.min_fingerprint_cosine_similarity =
                minSimilarity->value();
            options.association_threshold =
                associationThreshold->value();

            audition::HeuristicSourceIdentityResolver resolver{
                registry,
                options};

            const auto first = resolver.observe(
                identityObservation(
                    11U,
                    0,
                    fingerprint(
                        static_cast<float>(firstX->value()),
                        static_cast<float>(firstY->value()))));

            resolver.endTrack(
                audition::SpatialTrackId{11U},
                ts(1'000'000'000LL));

            const auto second = resolver.observe(
                identityObservation(
                    37U,
                    1'100'000'000LL,
                    fingerprint(
                        static_cast<float>(secondX->value()),
                        static_cast<float>(secondY->value()))));

            const bool same = first.source_id == second.source_id;

            std::ostringstream text;
            text << "first.track_id=11\n"
                 << "first.source_id=" << first.source_id.value() << "\n"
                 << "first.new=" << (first.newly_created ? "yes" : "no") << "\n\n"
                 << "second.track_id=37\n"
                 << "second.source_id=" << second.source_id.value() << "\n"
                 << "second.new=" << (second.newly_created ? "yes" : "no") << "\n"
                 << "second.association_score="
                 << second.confidence.value() << "\n\n"
                 << "persistent_source_reacquired="
                 << (same && !second.newly_created ? "yes" : "no") << "\n"
                 << "registry.sources=" << registry.list().size();
            output->setPlainText(QString::fromStdString(text.str()));
        } catch (const std::exception& error) {
            output->setPlainText(errorText(error));
        }
    });

    return page;
}

QWidget* DemoWindow::createEventsTab() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout{page};

    layout->addWidget(makeDescription(
        "Exercise the explicit AcousticEvent lifecycle. Event boundaries are "
        "caller-controlled; updates are transactional and carry acoustic "
        "identity/geometry/classification/speech annotations."));

    auto* form = new QFormLayout;
    auto* label = new QLineEdit{"alarm"};
    auto* transcriptText = new QLineEdit{"please help"};
    auto* range = makeDouble(0.0, 1000.0, 2.5);
    auto* classProbability = makeDouble(0.0, 1.0, 0.90);
    auto* transcriptProbability = makeDouble(0.0, 1.0, 0.85);

    form->addRow("Classification label", label);
    form->addRow("Transcript", transcriptText);
    form->addRow("Range [m]", range);
    form->addRow("Class probability", classProbability);
    form->addRow("Transcript probability", transcriptProbability);
    layout->addLayout(form);

    auto* run = new QPushButton{"Assemble event"};
    auto* output = makeOutput();
    layout->addWidget(run);
    layout->addWidget(output);

    connect(run, &QPushButton::clicked, page, [=]() {
        try {
            audition::AcousticEventAssembler assembler;

            audition::SpatialTrack track{};
            track.track_id = audition::SpatialTrackId{7U};
            track.source_id = audition::AcousticSourceId{3U};
            track.first_seen = ts(0);
            track.last_seen = ts(10);
            track.direction.direction =
                audition::Direction3D::fromVector({1.0, 0.0, 0.0});
            track.range = audition::RangeEstimate{
                {range->value(), 0.25},
                audition::Probability::zero(),
                audition::RangeEstimate::Method::Fused};

            const auto eventId = assembler.begin(ts(0));
            assembler.updateFromTrack(eventId, track);

            audition::AcousticEventPatch classificationPatch{};
            classificationPatch.timestamp = ts(20);
            audition::ClassificationResult classification{};
            classification.classes.push_back(
                {label->text().toStdString(),
                 audition::Probability::from(
                     classProbability->value())});
            classificationPatch.classification =
                std::move(classification);
            assembler.update(eventId, classificationPatch);

            audition::AcousticEventPatch transcriptPatch{};
            transcriptPatch.timestamp = ts(30);
            audition::Transcript transcript{};
            transcript.segment_id = audition::SpeechSegmentId{1U};
            transcript.text = transcriptText->text().toStdString();
            transcript.language = "en";
            transcript.confidence =
                audition::Probability::from(
                    transcriptProbability->value());
            transcriptPatch.transcript = std::move(transcript);
            assembler.update(eventId, transcriptPatch);

            const auto event = assembler.finish(eventId, ts(40));

            std::ostringstream text;
            text << "event.id=" << event.event_id.value() << "\n"
                 << "event.start_ns=" << event.start_time.nanoseconds() << "\n"
                 << "event.end_ns=" << event.end_time.nanoseconds() << "\n"
                 << "event.track_id=" << event.track_id->value() << "\n"
                 << "event.source_id=" << event.source_id->value() << "\n"
                 << "event.range_m=" << event.range->distance_m.mean << "\n"
                 << "event.class="
                 << event.classification->classes.front().label << "\n"
                 << "event.transcript=" << event.transcript->text << "\n"
                 << "assembler.active_after_finish="
                 << assembler.activeCount();
            output->setPlainText(QString::fromStdString(text.str()));
        } catch (const std::exception& error) {
            output->setPlainText(errorText(error));
        }
    });

    return page;
}

QWidget* DemoWindow::createPipelineTab() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout{page};

    layout->addWidget(makeDescription(
        "Run a small dependency-free synthetic composition: dBFS observation "
        "→ SPL calibration → acoustic range prior → SpatialTrack → temporal "
        "smoothing → persistent AcousticSourceId → AcousticEvent."));

    auto* measuredDbfs = makeDouble(-120.0, 20.0, -40.0);
    auto* run = new QPushButton{"Run synthetic acoustic pipeline"};
    auto* output = makeOutput();

    auto* form = new QFormLayout;
    form->addRow("Synthetic measured level [dBFS]", measuredDbfs);
    layout->addLayout(form);
    layout->addWidget(run);
    layout->addWidget(output);

    connect(run, &QPushButton::clicked, page, [=]() {
        try {
            audition::SoundPressureCalibrationProfile profile{};
            profile.profile_id = "pipeline-demo";
            profile.signal_path_id = "pipeline-mic";
            profile.reference_level_db_spl = {94.0, 0.25};
            profile.measured_level_dbfs = {-20.0, 0.25};
            profile.weighting = audition::SoundLevelWeighting::Z;

            audition::SoundPressureLevelCalibrator calibrator{profile};

            audition::DbfsLevelObservation dbfs{};
            dbfs.timestamp = ts(0);
            dbfs.level_dbfs = measuredDbfs->value();
            dbfs.variance_db2 = 0.25;
            dbfs.weighting = audition::SoundLevelWeighting::Z;
            dbfs.signal_path_id = "pipeline-mic";

            const auto spl = calibrator.calibrate(dbfs);
            if (!spl.has_value()) {
                output->setPlainText("No finite SPL.");
                return;
            }

            audition::SourceLevelPrior priors[] = {
                {
                    "alarm",
                    {80.0, 4.0},
                    1.0,
                    1.0,
                    audition::SoundLevelWeighting::Z,
                },
            };

            audition::RangeEstimationInput rangeInput{};
            rangeInput.sound_level = *spl;
            rangeInput.source_level_priors = priors;
            audition::SoundLevelRangePriorEstimator rangeEstimator;
            const auto estimatedRange =
                rangeEstimator.estimate(rangeInput);
            if (!estimatedRange.has_value()) {
                output->setPlainText("No range prior.");
                return;
            }

            audition::SpatialTrack track{};
            track.track_id = audition::SpatialTrackId{11U};
            track.first_seen = ts(0);
            track.last_seen = ts(10);
            track.range = *estimatedRange;
            track.fingerprint = fingerprint(1.0F, 0.0F);

            audition::TemporalSpatialTrackSmoother smoother;
            smoother.update(track);

            audition::InMemoryAcousticSourceRegistry registry;
            audition::HeuristicSourceIdentityResolver resolver{registry};
            NoopFusion fusion;
            audition::SpatialIdentityCoordinator identity{
                fusion,
                resolver};
            const auto decision = identity.observe(track);

            audition::AcousticEventAssembler events;
            const auto eventId = events.begin(ts(0));
            events.updateFromTrack(eventId, track);

            audition::AcousticEventPatch patch{};
            patch.timestamp = ts(20);
            audition::ClassificationResult classification{};
            classification.classes.push_back(
                {"alarm", audition::Probability::from(0.95)});
            patch.classification = std::move(classification);
            events.update(eventId, patch);
            const auto event = events.finish(eventId, ts(30));

            std::ostringstream text;
            text << std::fixed << std::setprecision(4)
                 << "stage.calibrated_spl_db=" << spl->level_db_spl.mean << "\n"
                 << "stage.range_m=" << estimatedRange->distance_m.mean << "\n"
                 << "stage.range_sigma_m="
                 << std::sqrt(estimatedRange->distance_m.variance) << "\n"
                 << "stage.track_id=" << track.track_id.value() << "\n"
                 << "stage.source_id=" << decision.source_id.value() << "\n"
                 << "stage.source_new="
                 << (decision.newly_created ? "yes" : "no") << "\n"
                 << "stage.event_id=" << event.event_id.value() << "\n"
                 << "stage.event_class="
                 << event.classification->classes.front().label << "\n\n"
                 << "pipeline=ok";
            output->setPlainText(QString::fromStdString(text.str()));
        } catch (const std::exception& error) {
            output->setPlainText(errorText(error));
        }
    });

    return page;
}

QWidget* DemoWindow::createCapabilitiesTab() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout{page};

    layout->addWidget(makeDescription(
        "Optional adapters are compile-time opt-ins. This table shows what the "
        "current demo binary was built with and what each adapter exposes. "
        "Model-backed adapters additionally need external model artifacts; "
        "libaudition does not bundle models."));

    auto* table = new QTableWidget{0, 5};
    table->setHorizontalHeaderLabels(
        {"Feature", "Adapter", "Built", "Input/assets", "Interface / capability"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);

    struct Row {
        const char* feature;
        const char* adapter;
        bool built;
        const char* assets;
        const char* api;
    };

    const Row rows[] = {
        {"Spatial SSL/SST/SSS", "ODAS",
         LIBAUDITION_DEMO_HAS_ODAS != 0,
         "microphone geometry + multichannel PCM",
         "ISpatialAudioEngine"},
        {"Resampling", "libsamplerate",
         LIBAUDITION_DEMO_HAS_SAMPLERATE != 0,
         "PCM + source/target sample rates",
         "IAudioResampler"},
        {"Echo cancellation", "WebRTC AEC3",
         LIBAUDITION_DEMO_HAS_AEC3 != 0,
         "capture PCM + playback reference",
         "IEchoCanceller"},
        {"VAD / ASR / KWS / language ID", "sherpa-onnx",
         LIBAUDITION_DEMO_HAS_SHERPA != 0,
         "external sherpa model files",
         "IVad / IAsr / IKeywordSpotter / ILanguageIdentifier"},
        {"Speaker embeddings / verify / diarize", "sherpa-onnx",
         LIBAUDITION_DEMO_HAS_SHERPA != 0,
         "external speaker/diarization models",
         "speaker interfaces"},
        {"Speech denoising", "sherpa-onnx",
         LIBAUDITION_DEMO_HAS_SHERPA != 0,
         "GTCRN or DPDFNet model",
         "INoiseSuppressor"},
        {"TTS", "sherpa-onnx",
         LIBAUDITION_DEMO_HAS_SHERPA_TTS != 0,
         "VITS/Piper, Matcha or Kokoro model assets",
         "ISpeechSynthesizer"},
        {"Audio tagging", "sherpa-onnx",
         LIBAUDITION_DEMO_HAS_SHERPA != 0,
         "Zipformer/CED tagger + labels",
         "IAudioClassifier"},
        {"Audio embedding / open vocabulary", "CLAP ONNX",
         LIBAUDITION_DEMO_HAS_CLAP != 0,
         "audio/text ONNX models + tokenizer",
         "IAudioEmbedder / IOpenVocabularyAudioClassifier"},
        {"Authenticity", "AASIST ONNX",
         LIBAUDITION_DEMO_HAS_AASIST != 0,
         "AASIST waveform ONNX model",
         "IAudioAuthenticityDetector"},
        {"Pitch / acoustic voice features", "WORLD",
         LIBAUDITION_DEMO_HAS_WORLD != 0,
         "mono speech PCM; no model file",
         "IVoiceTraitsEstimator / IVoiceAcousticAnalyzer"},
        {"Acoustic position fusion", "GTSAM",
         LIBAUDITION_DEMO_HAS_GTSAM != 0,
         "bearing/range/position acoustic observations",
         "ISpatialFusion"},
    };

    for (const auto& row : rows) {
        const int index = table->rowCount();
        table->insertRow(index);
        table->setItem(index, 0, new QTableWidgetItem{row.feature});
        table->setItem(index, 1, new QTableWidgetItem{row.adapter});
        table->setItem(
            index,
            2,
            new QTableWidgetItem{row.built ? "yes" : "no"});
        table->setItem(index, 3, new QTableWidgetItem{row.assets});
        table->setItem(index, 4, new QTableWidgetItem{row.api});
    }

    layout->addWidget(table);
    layout->addWidget(makeDescription(
        "The GUI intentionally does not guess backend model paths or download "
        "models. A next layer can add model-specific runner panels on top of "
        "this feature matrix without changing libaudition itself."));
    return page;
}

bool DemoWindow::runSelfTest(QString* report) {
    try {
        verifyWorkbench(centralWidget());
        verifyWavRoundTrip();

        std::vector<float> samples{
            -1.0F, 0.0F, 1.0F, 0.0F};
        audition::AudioBuffer audio{
            std::move(samples),
            {16000U, 1U, audition::AudioLayout::Interleaved},
            ts(0)};
        const auto quality =
            audition::dsp::analyzeQuality(audio.view(), 0.99);
        if (quality.aggregate.peak_linear < 0.99) {
            if (report != nullptr) {
                *report = "self-test failed: DSP peak";
            }
            return false;
        }

        audition::SoundPressureCalibrationProfile profile{};
        profile.profile_id = "self-test";
        profile.signal_path_id = "self-test-mic";
        profile.reference_level_db_spl = {94.0, 0.0};
        profile.measured_level_dbfs = {-20.0, 0.0};
        profile.weighting = audition::SoundLevelWeighting::Z;
        audition::SoundPressureLevelCalibrator calibrator{profile};

        audition::DbfsLevelObservation dbfs{};
        dbfs.timestamp = ts(0);
        dbfs.level_dbfs = -40.0;
        dbfs.weighting = audition::SoundLevelWeighting::Z;
        dbfs.signal_path_id = "self-test-mic";
        const auto spl = calibrator.calibrate(dbfs);
        if (!spl.has_value() || std::abs(spl->level_db_spl.mean - 74.0) > 1.0e-9) {
            if (report != nullptr) {
                *report = "self-test failed: SPL calibration";
            }
            return false;
        }

        audition::SourceLevelPrior priors[] = {
            {
                "source",
                {94.0, 0.0},
                1.0,
                1.0,
                audition::SoundLevelWeighting::Z,
            },
        };
        audition::RangeEstimationInput rangeInput{};
        rangeInput.sound_level = *spl;
        rangeInput.source_level_priors = priors;
        audition::SoundLevelRangePriorEstimator estimator;
        const auto range = estimator.estimate(rangeInput);
        if (!range.has_value() ||
            std::abs(range->distance_m.mean - 10.0) > 1.0e-9) {
            if (report != nullptr) {
                *report = "self-test failed: range prior";
            }
            return false;
        }

        audition::TemporalSpatialTrackSmoother smoother;
        audition::SpatialTrack track{};
        track.track_id = audition::SpatialTrackId{1U};
        track.first_seen = ts(0);
        track.last_seen = ts(0);
        track.range = *range;
        smoother.update(track);

        audition::InMemoryAcousticSourceRegistry registry;
        audition::HeuristicSourceIdentityResolver resolver{registry};
        const auto first = resolver.observe(
            identityObservation(1U, 0, fingerprint(1.0F, 0.0F)));
        resolver.endTrack(audition::SpatialTrackId{1U}, ts(1));
        const auto second = resolver.observe(
            identityObservation(2U, 2, fingerprint(0.999F, 0.01F)));
        if (first.source_id != second.source_id) {
            if (report != nullptr) {
                *report = "self-test failed: source identity";
            }
            return false;
        }

        audition::AcousticEventAssembler events;
        const auto eventId = events.begin(ts(0));
        audition::AcousticEventPatch patch{};
        patch.timestamp = ts(1);
        patch.source_id = second.source_id;
        events.update(eventId, patch);
        const auto event = events.finish(eventId, ts(2));
        if (!event.source_id.has_value() ||
            *event.source_id != second.source_id) {
            if (report != nullptr) {
                *report = "self-test failed: AcousticEvent";
            }
            return false;
        }

        if (report != nullptr) {
            *report = "qt-demo-self-test=ok";
        }
        return true;
    } catch (const std::exception& error) {
        if (report != nullptr) {
            *report = errorText(error);
        }
        return false;
    }
}
