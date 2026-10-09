#include "optional_backend_workbench.hpp"

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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if LIBAUDITION_DEMO_HAS_SAMPLERATE
#include <audition/backends/samplerate.hpp>
#endif

#if LIBAUDITION_DEMO_HAS_AEC3
#include <audition/backends/webrtc_aec3.hpp>
#endif

#if LIBAUDITION_DEMO_HAS_GTSAM
#include <audition/backends/gtsam.hpp>
#endif

namespace {

[[maybe_unused]] QString exceptionText(const std::exception& error) {
    return QStringLiteral("error: ") + QString::fromUtf8(error.what());
}

QLabel* description(const QString& text) {
    auto* label = new QLabel{text};
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

[[maybe_unused]] QPlainTextEdit* outputBox() {
    auto* output = new QPlainTextEdit;
    output->setReadOnly(true);
    output->setMinimumHeight(220);
    return output;
}

[[maybe_unused]] QSpinBox* intBox(
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

[[maybe_unused]] QWidget* inputPathEditor(
    QLineEdit*& edit,
    QWidget* parent,
    const QString& filter = QStringLiteral("All files (*)")) {
    auto* row = new QWidget{parent};
    auto* layout = new QHBoxLayout{row};
    layout->setContentsMargins(0, 0, 0, 0);
    edit = new QLineEdit{row};
    auto* choose = new QPushButton{"Browse…", row};
    layout->addWidget(edit, 1);
    layout->addWidget(choose);
    QObject::connect(
        choose,
        &QPushButton::clicked,
        row,
        [edit, filter]() {
            const auto path = QFileDialog::getOpenFileName(
                nullptr,
                "Choose file",
                edit->text(),
                filter);
            if (!path.isEmpty()) {
                edit->setText(path);
            }
        });
    return row;
}

[[maybe_unused]] QWidget* outputPathEditor(
    QLineEdit*& edit,
    QWidget* parent,
    const QString& defaultName) {
    auto* row = new QWidget{parent};
    auto* layout = new QHBoxLayout{row};
    layout->setContentsMargins(0, 0, 0, 0);
    edit = new QLineEdit{defaultName, row};
    auto* choose = new QPushButton{"Save as…", row};
    layout->addWidget(edit, 1);
    layout->addWidget(choose);
    QObject::connect(
        choose,
        &QPushButton::clicked,
        row,
        [edit]() {
            const auto path = QFileDialog::getSaveFileName(
                nullptr,
                "Save WAV",
                edit->text(),
                "WAV audio (*.wav)");
            if (!path.isEmpty()) {
                edit->setText(path);
            }
        });
    return row;
}

[[maybe_unused]] std::filesystem::path fsPath(const QLineEdit* edit) {
    return std::filesystem::path{edit->text().toStdString()};
}

[[maybe_unused]] demo::LoadedWav loadWav(const QLineEdit* edit) {
    if (edit->text().trimmed().isEmpty()) {
        throw std::runtime_error{"WAV path is required"};
    }
    return demo::loadWav(fsPath(edit));
}

[[maybe_unused]] QString audioSummary(audition::AudioView audio) {
    std::ostringstream text;
    text << "sample_rate_hz=" << audio.format().sample_rate_hz
         << " channels=" << audio.format().channel_count
         << " frames=" << audio.frameCount()
         << " duration_s=" << audio.duration().seconds();
    return QString::fromStdString(text.str());
}

[[maybe_unused]] audition::AudioBuffer sliceFrames(
    audition::AudioView audio,
    std::size_t firstFrame,
    std::size_t frameCount,
    std::uint64_t sequence) {
    if (firstFrame + frameCount > audio.frameCount()) {
        throw std::runtime_error{"Audio slice exceeds source frame count"};
    }

    const auto channels =
        static_cast<std::size_t>(audio.format().channel_count);
    std::vector<float> samples(frameCount * channels);

    if (audio.format().layout == audition::AudioLayout::Interleaved) {
        const auto firstSample = firstFrame * channels;
        std::copy_n(
            audio.data() + static_cast<std::ptrdiff_t>(firstSample),
            frameCount * channels,
            samples.data());
    } else {
        for (std::size_t channel = 0U;
             channel < channels;
             ++channel) {
            const auto source = audio.channel(channel);
            for (std::size_t frame = 0U;
                 frame < frameCount;
                 ++frame) {
                samples[channel * frameCount + frame] =
                    source[firstFrame + frame];
            }
        }
    }

    return audition::AudioBuffer{
        std::move(samples),
        audio.format(),
        audio.captureTime().advancedBy(
            audition::Duration{
                static_cast<std::int64_t>(
                    (1'000'000'000ULL * firstFrame) /
                    audio.format().sample_rate_hz)}),
        sequence};
}

[[maybe_unused]] void appendAudio(
    std::vector<float>& destination,
    const audition::AudioBuffer& source) {
    if (source.format().layout != audition::AudioLayout::Interleaved) {
        throw std::runtime_error{
            "Workbench output aggregation expects interleaved backend output"};
    }
    const auto& samples = source.samples();
    destination.insert(
        destination.end(),
        samples.begin(),
        samples.end());
}

[[maybe_unused]] QWidget* unavailablePanel(
    const QString& name,
    const QString& option,
    QWidget* parent) {
    auto* page = new QWidget{parent};
    auto* layout = new QVBoxLayout{page};
    layout->addWidget(description(
        QStringLiteral("<h3>%1 backend is not linked</h3>"
                       "<p>Reconfigure with <code>%2=ON</code> "
                       "to enable this real runner.</p>")
            .arg(name, option)));
    layout->addStretch(1);
    return page;
}

QWidget* createSampleratePanel(QWidget* parent) {
#if LIBAUDITION_DEMO_HAS_SAMPLERATE
    auto* page = new QWidget{parent};
    auto* layout = new QVBoxLayout{page};
    layout->addWidget(description(
        "Runs libsamplerate through IAudioResamplerSession and writes a "
        "real resampled PCM16 WAV. The original channel count is retained; "
        "no hidden downmix is performed."));

    auto* form = new QFormLayout;
    QLineEdit* inputPath = nullptr;
    QLineEdit* outputPath = nullptr;
    auto* outputRate = intBox(1000, 384000, 48000, 1000);
    auto* converter = new QComboBox{page};
    converter->addItem("Sinc best");
    converter->addItem("Sinc medium");
    converter->addItem("Sinc fast");
    converter->addItem("Zero-order hold");
    converter->addItem("Linear");
    converter->setCurrentIndex(1);

    form->addRow(
        "Input WAV",
        inputPathEditor(
            inputPath,
            page,
            "WAV audio (*.wav *.WAV);;All files (*)"));
    form->addRow("Output sample rate", outputRate);
    form->addRow("Converter", converter);
    form->addRow(
        "Output WAV",
        outputPathEditor(
            outputPath,
            page,
            "libaudition_resampled.wav"));
    layout->addLayout(form);

    auto* run = new QPushButton{"Resample WAV"};
    auto* output = outputBox();
    layout->addWidget(run);
    layout->addWidget(output);

    QObject::connect(
        run,
        &QPushButton::clicked,
        page,
        [=]() {
            try {
                const auto wav = loadWav(inputPath);

                audition::SamplerateOptions options{};
                switch (converter->currentIndex()) {
                case 0:
                    options.converter =
                        audition::SamplerateConverter::SincBest;
                    break;
                case 1:
                    options.converter =
                        audition::SamplerateConverter::SincMedium;
                    break;
                case 2:
                    options.converter =
                        audition::SamplerateConverter::SincFast;
                    break;
                case 3:
                    options.converter =
                        audition::SamplerateConverter::ZeroOrderHold;
                    break;
                case 4:
                    options.converter =
                        audition::SamplerateConverter::Linear;
                    break;
                default:
                    throw std::runtime_error{"Unknown converter selection"};
                }

                audition::SamplerateResampler resampler{options};
                audition::ResamplerConfig config{};
                config.input_sample_rate_hz =
                    wav.audio.format().sample_rate_hz;
                config.output_sample_rate_hz =
                    static_cast<std::uint32_t>(outputRate->value());
                config.channel_count =
                    wav.audio.format().channel_count;

                auto session = resampler.createSession(config);
                auto mainOutput = session->process(wav.audio.view());
                auto tailOutput = session->flush();

                if (mainOutput.format().layout !=
                        audition::AudioLayout::Interleaved ||
                    tailOutput.format().layout !=
                        audition::AudioLayout::Interleaved) {
                    throw std::runtime_error{
                        "WAV workbench expects interleaved resampler output"};
                }

                std::vector<float> samples;
                samples.reserve(
                    mainOutput.samples().size() +
                    tailOutput.samples().size());
                appendAudio(samples, mainOutput);
                appendAudio(samples, tailOutput);

                audition::AudioBuffer resampled{
                    std::move(samples),
                    mainOutput.format(),
                    mainOutput.captureTime(),
                    mainOutput.sequenceNumber()};
                demo::saveWavPcm16(
                    fsPath(outputPath),
                    resampled.view());

                std::ostringstream text;
                text << "backend=" << resampler.backendInfo().name
                     << "\ninput="
                     << audioSummary(wav.audio.view()).toStdString()
                     << "\noutput="
                     << audioSummary(resampled.view()).toStdString()
                     << "\nflush_frames="
                     << tailOutput.frameCount()
                     << "\nsaved="
                     << outputPath->text().toStdString();
                output->setPlainText(
                    QString::fromStdString(text.str()));
            } catch (const std::exception& error) {
                output->setPlainText(exceptionText(error));
            }
        });

    return page;
#else
    return unavailablePanel(
        "libsamplerate",
        "-DLIBAUDITION_WITH_LIBSAMPLERATE",
        parent);
#endif
}

QWidget* createAec3Panel(QWidget* parent) {
#if LIBAUDITION_DEMO_HAS_AEC3
    auto* page = new QWidget{parent};
    auto* layout = new QVBoxLayout{page};
    layout->addWidget(description(
        "Runs WebRTC AEC3 with a playback/reference WAV and a captured WAV. "
        "The runner feeds exact 10 ms frames as required by AEC3, processes "
        "the common complete duration, exposes stream delay, and writes the "
        "echo-cancelled capture to WAV. Rates must match and be 16/32/48 kHz."));

    auto* form = new QFormLayout;
    QLineEdit* capturePath = nullptr;
    QLineEdit* referencePath = nullptr;
    QLineEdit* outputPath = nullptr;
    auto* delayMs = intBox(0, 60000, 0, 10);
    auto* initialState =
        doubleBox(0.001, 30.0, 0.5, 3, 0.1);
    auto* conservative =
        new QCheckBox{"Conservative initial phase"};

    form->addRow(
        "Captured WAV",
        inputPathEditor(
            capturePath,
            page,
            "WAV audio (*.wav *.WAV);;All files (*)"));
    form->addRow(
        "Playback/reference WAV",
        inputPathEditor(
            referencePath,
            page,
            "WAV audio (*.wav *.WAV);;All files (*)"));
    form->addRow("Stream delay [ms]", delayMs);
    form->addRow(
        "Filter initial state [s]",
        initialState);
    form->addRow(conservative);
    form->addRow(
        "Output WAV",
        outputPathEditor(
            outputPath,
            page,
            "libaudition_aec3.wav"));
    layout->addLayout(form);

    auto* run = new QPushButton{"Run AEC3"};
    auto* output = outputBox();
    layout->addWidget(run);
    layout->addWidget(output);

    QObject::connect(
        run,
        &QPushButton::clicked,
        page,
        [=]() {
            try {
                const auto capture = loadWav(capturePath);
                const auto reference = loadWav(referencePath);

                audition::WebRtcAec3Options options{};
                options.filter_initial_state_seconds =
                    initialState->value();
                options.conservative_initial_phase =
                    conservative->isChecked();

                audition::WebRtcAec3EchoCanceller canceller{
                    options};
                auto session = canceller.createSession(
                    capture.audio.format(),
                    reference.audio.format());
                session->setStreamDelay(
                    audition::Duration{
                        static_cast<std::int64_t>(
                            delayMs->value()) *
                        1'000'000});

                const auto sampleRate =
                    capture.audio.format().sample_rate_hz;
                const std::size_t frameSize =
                    static_cast<std::size_t>(
                        sampleRate / 100U);
                const std::size_t commonFrames =
                    std::min(
                        capture.audio.frameCount(),
                        reference.audio.frameCount());
                const std::size_t blocks =
                    commonFrames / frameSize;
                if (blocks == 0U) {
                    throw std::runtime_error{
                        "AEC3 needs at least one complete 10 ms frame"};
                }

                std::vector<float> samples;
                samples.reserve(
                    blocks * frameSize *
                    static_cast<std::size_t>(
                        capture.audio.format().channel_count));

                for (std::size_t block = 0U;
                     block < blocks;
                     ++block) {
                    const auto offset = block * frameSize;
                    auto referenceFrame = sliceFrames(
                        reference.audio.view(),
                        offset,
                        frameSize,
                        static_cast<std::uint64_t>(block));
                    auto captureFrame = sliceFrames(
                        capture.audio.view(),
                        offset,
                        frameSize,
                        static_cast<std::uint64_t>(block));
                    session->acceptReference(
                        referenceFrame.view());
                    auto processed =
                        session->process(captureFrame.view());
                    appendAudio(samples, processed);
                }

                audition::AudioBuffer processed{
                    std::move(samples),
                    capture.audio.format(),
                    capture.audio.captureTime(),
                    capture.audio.sequenceNumber()};
                demo::saveWavPcm16(
                    fsPath(outputPath),
                    processed.view());

                const auto metrics = session->metrics();
                std::ostringstream text;
                text << "backend="
                     << canceller.backendInfo().name
                     << "\ncapture="
                     << audioSummary(capture.audio.view()).toStdString()
                     << "\nreference="
                     << audioSummary(reference.audio.view()).toStdString()
                     << "\nframe_size="
                     << frameSize
                     << "\nprocessed_blocks="
                     << blocks
                     << "\nprocessed_frames="
                     << processed.frameCount()
                     << "\ndropped_capture_tail_frames="
                     << (capture.audio.frameCount() -
                         blocks * frameSize)
                     << "\ndropped_reference_tail_frames="
                     << (reference.audio.frameCount() -
                         blocks * frameSize)
                     << "\necho_return_loss_db=";
                if (metrics.echo_return_loss_db.has_value()) {
                    text << *metrics.echo_return_loss_db;
                } else {
                    text << "<absent>";
                }
                text << "\necho_return_loss_enhancement_db=";
                if (metrics.echo_return_loss_enhancement_db.has_value()) {
                    text << *metrics.echo_return_loss_enhancement_db;
                } else {
                    text << "<absent>";
                }
                text << "\nestimated_delay_ms=";
                if (metrics.estimated_delay.has_value()) {
                    text << (
                        metrics.estimated_delay->seconds() *
                        1000.0);
                } else {
                    text << "<absent>";
                }
                text << "\nsaved="
                     << outputPath->text().toStdString();

                output->setPlainText(
                    QString::fromStdString(text.str()));
            } catch (const std::exception& error) {
                output->setPlainText(exceptionText(error));
            }
        });

    return page;
#else
    return unavailablePanel(
        "WebRTC AEC3",
        "-DLIBAUDITION_WITH_WEBRTC_AEC3",
        parent);
#endif
}

#if LIBAUDITION_DEMO_HAS_GTSAM

std::vector<double> csvNumbers(
    const QString& line,
    std::size_t expected,
    const char* role) {
    const auto fields =
        line.split(',', Qt::SkipEmptyParts);
    if (static_cast<std::size_t>(fields.size()) != expected) {
        throw std::runtime_error{
            std::string{role} + " line must have " +
            std::to_string(expected) +
            " comma-separated numeric fields"};
    }

    std::vector<double> values;
    values.reserve(expected);
    for (const auto& field : fields) {
        bool ok = false;
        const auto value =
            field.trimmed().toDouble(&ok);
        if (!ok) {
            throw std::runtime_error{
                std::string{role} +
                " contains a non-numeric field"};
        }
        values.push_back(value);
    }
    return values;
}

audition::Timestamp observationTime(std::size_t index) {
    return audition::Timestamp{
        static_cast<std::int64_t>(index),
        {audition::ClockDomain::Monotonic, 0U}};
}

audition::Covariance3 diagonalCovariance(
    double x,
    double y,
    double z) {
    return audition::Covariance3{
        x, 0.0, 0.0,
        0.0, y, 0.0,
        0.0, 0.0, z};
}

std::vector<audition::BearingObservation> parseBearings(
    const QString& text) {
    std::vector<audition::BearingObservation> result;
    for (const auto& raw :
         text.split('\n', Qt::SkipEmptyParts)) {
        const auto line = raw.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const auto v = csvNumbers(
            line,
            7U,
            "Bearing");
        audition::Pose3D pose{};
        pose.position = {v[0], v[1], v[2]};
        audition::DirectionEstimate estimate{};
        estimate.direction =
            audition::Direction3D::fromVector(
                {v[3], v[4], v[5]});
        estimate.angular_variance_rad2 = v[6];
        estimate.confidence =
            audition::Probability::one();
        result.push_back(
            {
                observationTime(result.size()),
                pose,
                estimate,
            });
    }
    return result;
}

std::vector<audition::ScalarRangeObservation> parseRanges(
    const QString& text) {
    std::vector<audition::ScalarRangeObservation> result;
    for (const auto& raw :
         text.split('\n', Qt::SkipEmptyParts)) {
        const auto line = raw.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const auto v = csvNumbers(
            line,
            5U,
            "Range");
        audition::Pose3D pose{};
        pose.position = {v[0], v[1], v[2]};
        result.push_back(
            {
                observationTime(result.size()),
                {v[3], v[4]},
                audition::Probability::one(),
                audition::RangeEstimate::Method::Unknown,
                pose,
            });
    }
    return result;
}

std::vector<audition::PositionObservation> parsePositions(
    const QString& text) {
    std::vector<audition::PositionObservation> result;
    for (const auto& raw :
         text.split('\n', Qt::SkipEmptyParts)) {
        const auto line = raw.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const auto v = csvNumbers(
            line,
            6U,
            "Position");
        audition::PositionEstimate estimate{};
        estimate.mean_m = {v[0], v[1], v[2]};
        estimate.covariance_m2 =
            diagonalCovariance(
                v[3],
                v[4],
                v[5]);
        estimate.confidence =
            audition::Probability::one();
        result.push_back(
            {
                observationTime(result.size()),
                estimate,
            });
    }
    return result;
}

#endif

QWidget* createGtsamPanel(QWidget* parent) {
#if LIBAUDITION_DEMO_HAS_GTSAM
    auto* page = new QWidget{parent};
    auto* layout = new QVBoxLayout{page};
    layout->addWidget(description(
        "Runs the acoustic-only GTSAM ISpatialFusion backend. Bearings use "
        "identity sensor orientation in this workbench. Enter one observation "
        "per line: bearing = sx,sy,sz,dx,dy,dz,angular_variance; "
        "range = sx,sy,sz,distance,variance; "
        "position = x,y,z,var_x,var_y,var_z. Empty groups are allowed."));

    auto* form = new QFormLayout;
    auto* bearings = new QPlainTextEdit{page};
    bearings->setPlaceholderText(
        "0,0,0,1,0,0,0.01\n"
        "0,2,0,1,-0.5,0,0.01");
    bearings->setMinimumHeight(90);
    auto* ranges = new QPlainTextEdit{page};
    ranges->setPlaceholderText(
        "0,0,0,2.0,0.04");
    ranges->setMinimumHeight(75);
    auto* positions = new QPlainTextEdit{page};
    positions->setPlaceholderText(
        "2,0,0,0.04,0.04,0.04");
    positions->setMinimumHeight(75);
    auto* bearingSigma =
        doubleBox(0.000001, 10.0, 0.15, 6, 0.01);
    auto* fallbackRange =
        doubleBox(0.000001, 100000.0, 2.0, 3, 0.5);
    auto* huber =
        new QCheckBox{"Enable Huber loss"};
    auto* huberK =
        doubleBox(0.000001, 1000.0, 1.345, 3, 0.1);
    auto* iterations =
        intBox(1, 100000, 50);

    form->addRow("Bearing observations", bearings);
    form->addRow("Range observations", ranges);
    form->addRow("Position observations", positions);
    form->addRow(
        "Default bearing sigma [rad]",
        bearingSigma);
    form->addRow(
        "Fallback initial range [m]",
        fallbackRange);
    form->addRow(huber);
    form->addRow("Huber k", huberK);
    form->addRow("Max iterations", iterations);
    layout->addLayout(form);

    auto* run = new QPushButton{"Fuse observations"};
    auto* output = outputBox();
    layout->addWidget(run);
    layout->addWidget(output);

    QObject::connect(
        run,
        &QPushButton::clicked,
        page,
        [=]() {
            try {
                const auto bearingValues =
                    parseBearings(
                        bearings->toPlainText());
                const auto rangeValues =
                    parseRanges(
                        ranges->toPlainText());
                const auto positionValues =
                    parsePositions(
                        positions->toPlainText());

                audition::GtsamSpatialFusionOptions options{};
                options.default_bearing_sigma_rad =
                    bearingSigma->value();
                options.fallback_initial_range_m =
                    fallbackRange->value();
                options.enable_huber_loss =
                    huber->isChecked();
                options.huber_k =
                    huberK->value();
                options.max_iterations =
                    static_cast<std::size_t>(
                        iterations->value());

                audition::GtsamSpatialFusion fusion{
                    options};
                const audition::SpatialFusionInput input{
                    {
                        bearingValues.data(),
                        bearingValues.size(),
                    },
                    {
                        rangeValues.data(),
                        rangeValues.size(),
                    },
                    {
                        positionValues.data(),
                        positionValues.size(),
                    },
                };

                const auto result = fusion.fuse(input);
                std::ostringstream text;
                text << "backend="
                     << fusion.backendInfo().name
                     << "\nbearings="
                     << bearingValues.size()
                     << "\nranges="
                     << rangeValues.size()
                     << "\npositions="
                     << positionValues.size()
                     << "\nresult=";
                if (!result.has_value()) {
                    text << "<no estimate>";
                } else {
                    text << "available"
                         << "\nmean_m=["
                         << result->mean_m.x << ", "
                         << result->mean_m.y << ", "
                         << result->mean_m.z << "]"
                         << "\nconfidence="
                         << result->confidence.value()
                         << "\ncovariance_m2=\n";
                    for (std::size_t row = 0U;
                         row < 3U;
                         ++row) {
                        text << "[";
                        for (std::size_t column = 0U;
                             column < 3U;
                             ++column) {
                            if (column != 0U) {
                                text << ", ";
                            }
                            text << result->covariance_m2[
                                row * 3U + column];
                        }
                        text << "]\n";
                    }
                }

                output->setPlainText(
                    QString::fromStdString(text.str()));
            } catch (const std::exception& error) {
                output->setPlainText(exceptionText(error));
            }
        });

    return page;
#else
    return unavailablePanel(
        "GTSAM",
        "-DLIBAUDITION_WITH_GTSAM",
        parent);
#endif
}

}  // namespace

void addOptionalBackendWorkbenchTabs(QTabWidget* tabs) {
    tabs->addTab(
        createSampleratePanel(tabs),
        "libsamplerate");
    tabs->addTab(
        createAec3Panel(tabs),
        "AEC3");
    tabs->addTab(
        createGtsamPanel(tabs),
        "GTSAM");
}
