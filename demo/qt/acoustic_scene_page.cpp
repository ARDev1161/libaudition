#include "acoustic_scene_page.hpp"
#include "alsa_live_capture.hpp"
#include "acoustic_sphere_widget.hpp"
#include "wav_io.hpp"

#if LIBAUDITION_DEMO_HAS_ODAS
#include <audition/backends/odas.hpp>
#endif

#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QSpinBox>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr double kPi = 3.14159265358979323846;

struct SceneFrame {
    double seconds{0.0};
    std::vector<AcousticSceneTrack> tracks{};
};

struct SceneReplay {
    std::vector<SceneFrame> frames{};
    QString error{};
    std::size_t hops{0};
};

[[maybe_unused]] std::vector<std::size_t> parseChannels(const QString& text) {
    std::vector<std::size_t> channels;
    for (const auto& token : text.split(',', Qt::SkipEmptyParts)) {
        bool ok = false;
        const auto value = token.trimmed().toULongLong(&ok);
        if (!ok || value > 255U) {
            throw std::runtime_error{"Expected comma-separated, zero-based input channels"};
        }
        channels.push_back(static_cast<std::size_t>(value));
    }
    if (channels.empty()) {
        throw std::runtime_error{"At least one input channel is required"};
    }
    return channels;
}

#if LIBAUDITION_DEMO_HAS_ODAS
std::vector<audition::MicrophoneGeometry> parseMicrophones(
    const QString& text) {
    std::vector<audition::MicrophoneGeometry> microphones;
    for (const auto& line : text.split('\n', Qt::SkipEmptyParts)) {
        const auto parts = line.split(',', Qt::KeepEmptyParts);
        if (parts.size() != 3) {
            throw std::runtime_error{"Microphone XYZ: exactly three numbers per line"};
        }
        bool valid[3] = {false, false, false};
        const double x = parts[0].trimmed().toDouble(&valid[0]);
        const double y = parts[1].trimmed().toDouble(&valid[1]);
        const double z = parts[2].trimmed().toDouble(&valid[2]);
        if (!valid[0] || !valid[1] || !valid[2]) {
            throw std::runtime_error{"Invalid microphone XYZ coordinate"};
        }
        microphones.push_back(audition::MicrophoneGeometry{{x, y, z}});
    }
    if (microphones.empty()) {
        throw std::runtime_error{"Missing microphone geometry"};
    }
    return microphones;
}

std::shared_ptr<SceneReplay> analyzeWav(
    const std::filesystem::path& wavFile,
    QString channels, QString geometry,
    std::uint32_t hopSize, std::uint32_t frameSize) {
    auto result = std::make_shared<SceneReplay>();
    try {
        const auto wav = demo::loadWav(wavFile);
        const auto format = wav.audio.format();
        audition::OdasOptions options{};
        options.input_channels = parseChannels(channels);
        options.microphone_array.microphones = parseMicrophones(geometry);
        options.sample_rate_hz = format.sample_rate_hz;
        options.hop_size = hopSize;
        options.frame_size = frameSize;
        // Direction visualization needs tracked sources, not SSS audio.
        options.sss.enabled = false;
        audition::OdasSpatialEngine engine{options};

        const std::size_t hop = options.hop_size;
        const std::size_t channelsInFile = format.channel_count;
        const auto& samples = wav.audio.samples();
        // Replay at about 20 visual frames/s, without blocking the GUI.
        const auto stride = std::max<std::size_t>(
            1, static_cast<std::size_t>(
                std::round(static_cast<double>(format.sample_rate_hz) /
                           (20.0 * static_cast<double>(hop)))));
        constexpr std::size_t maxFrames = 200000U;
        for (std::size_t offset = 0;
             offset + hop <= wav.audio.frameCount();
             offset += hop) {
            const auto begin = offset * channelsInFile;
            const auto count = hop * channelsInFile;
            std::vector<float> audio(
                samples.begin() + static_cast<std::ptrdiff_t>(begin),
                samples.begin() + static_cast<std::ptrdiff_t>(begin + count));
            audition::AudioBuffer block{
                std::move(audio), format,
                audition::Timestamp{
                    static_cast<std::int64_t>(
                        (static_cast<double>(offset) * 1.e9) /
                        format.sample_rate_hz),
                    {audition::ClockDomain::Monotonic, 1U}},
                static_cast<std::uint64_t>(result->hops)};
            auto spatial = engine.process(block.view());
            if (result->hops % stride == 0) {
                SceneFrame frame;
                frame.seconds = static_cast<double>(offset) /
                                format.sample_rate_hz;
                for (const auto& track : spatial.tracks) {
                    const auto v = track.direction.direction.vector();
                    frame.tracks.push_back({
                        track.track_id.value(),
                        QVector3D(static_cast<float>(v.x),
                                  static_cast<float>(v.y),
                                  static_cast<float>(v.z)),
                        track.activity.value()});
                }
                result->frames.push_back(std::move(frame));
                if (result->frames.size() >= maxFrames) {
                    throw std::runtime_error{
                        "WAV is too long for scene replay (200k frames maximum)"};
                }
            }
            ++result->hops;
        }
        if (result->hops == 0) {
            throw std::runtime_error{"WAV shorter than one ODAS hop"};
        }
    } catch (const std::exception& error) {
        result->frames.clear();
        result->error = QString::fromUtf8(error.what());
    }
    return result;
}
#endif

QString directionSummary(const AcousticSceneTrack& track) {
    const auto v = track.direction;
    const double az = std::atan2(v.y(), v.x()) * 180.0 / kPi;
    const double el = std::atan2(v.z(), std::hypot(v.x(), v.y())) * 180.0 / kPi;
    return QString("Track #%1\nAzimuth: %2°\nElevation: %3°\nActivity: %4\n"
                   "Classification: not configured\nASR: not configured")
        .arg(static_cast<qulonglong>(track.id))
        .arg(az, 0, 'f', 1)
        .arg(el, 0, 'f', 1)
        .arg(track.activity, 0, 'f', 2);
}
}  // namespace

QWidget* createAcousticScenePage(QWidget* parent) {
    auto* page = new QWidget{parent};
    page->setObjectName("acousticScenePage");
    auto* layout = new QVBoxLayout{page};
    auto* title = new QLabel{
        "<b>3D Acoustic Scene</b> — ODAS Studio-style unit sphere. "
        "Vertical position is elevation; markers indicate directions, "
        "<b>not distances</b>.", page};
    title->setWordWrap(true);
    layout->addWidget(title);

    auto* fileRow = new QHBoxLayout;
    auto* wavPath = new QLineEdit{page};
    wavPath->setPlaceholderText("Select multichannel RIFF/WAV for ODAS track replay");
    auto* browse = new QPushButton{"Browse WAV…", page};
    fileRow->addWidget(wavPath, 1);
    fileRow->addWidget(browse);
    layout->addLayout(fileRow);
    QObject::connect(browse, &QPushButton::clicked, page, [wavPath, page]() {
        const auto filename = QFileDialog::getOpenFileName(
            page, "Open multichannel WAV", wavPath->text(),
            "WAV audio (*.wav *.WAV)");
        if (!filename.isEmpty()) {
            wavPath->setText(filename);
        }
    });

    // Raw ALSA capture is an application-level concern, not an ODAS library
    // dependency. The device list is hardware only (hw:card,device).
    auto* liveRow = new QHBoxLayout;
    auto* deviceSelector = new QComboBox{page};
    deviceSelector->setObjectName("acousticSceneDevice");
    deviceSelector->setEditable(true);
    deviceSelector->setMinimumWidth(210);
    deviceSelector->setToolTip(
        "Detected capture PCM or manually enter hw:N,M; "
        "the stream must support S16_LE, 16 kHz and 6 channels.");
    auto* refreshDevices = new QPushButton{"Refresh ALSA devices", page};
    auto* startLive = new QPushButton{"Start live capture", page};
    auto* stopLive = new QPushButton{"Stop live capture", page};
    stopLive->setEnabled(false);
    auto* captureChannels = new QSpinBox{page};
    captureChannels->setObjectName("acousticSceneCaptureChannels");
    captureChannels->setRange(1, 32);
    captureChannels->setValue(6);
    captureChannels->setToolTip("Total hardware channels, including any reference channels");
    auto* captureRate = new QSpinBox{page};
    captureRate->setRange(8000, 192000);
    captureRate->setValue(16000);
    captureRate->setSuffix(" Hz");
    liveRow->addWidget(new QLabel{"ALSA PCM:", page});
    liveRow->addWidget(deviceSelector, 1);
    liveRow->addWidget(refreshDevices);
    liveRow->addWidget(new QLabel{"Channels:", page});
    liveRow->addWidget(captureChannels);
    liveRow->addWidget(new QLabel{"Rate:", page});
    liveRow->addWidget(captureRate);
    liveRow->addWidget(startLive);
    liveRow->addWidget(stopLive);
    layout->addLayout(liveRow);

    auto* deviceStatus = new QLabel{
        "Linux ALSA live capture. Refresh to detect USB audio devices.", page};
    deviceStatus->setObjectName("acousticSceneDeviceStatus");
    deviceStatus->setWordWrap(true);
    layout->addWidget(deviceStatus);
#if !LIBAUDITION_DEMO_HAS_ALSA
    refreshDevices->setEnabled(false);
    startLive->setEnabled(false);
    deviceStatus->setText(
        "Live capture disabled. Build on Linux with ODAS and libasound2-dev.");
#endif

    auto* configRow = new QHBoxLayout;
    auto* configForm = new QFormLayout;
    auto* map = new QLineEdit{"1,2,3,4", page};
    auto* geometry = new QPlainTextEdit{
        "-0.032,0,0\n0,-0.032,0\n0.032,0,0\n0,0.032,0", page};
    geometry->setMaximumHeight(95);
    auto* hop = new QSpinBox{page};
    hop->setRange(32, 4096);
    hop->setValue(128);
    auto* frame = new QSpinBox{page};
    frame->setRange(64, 8192);
    frame->setValue(256);
    configForm->addRow("ReSpeaker v2 example map (0-based)", map);
    configForm->addRow("Microphone XYZ [m] (verify geometry)", geometry);
    configForm->addRow("ODAS hop", hop);
    configForm->addRow("ODAS frame", frame);
    configRow->addLayout(configForm);
    layout->addLayout(configRow);

    auto* actions = new QHBoxLayout;
    auto* example = new QPushButton{"Show 3D example", page};
    auto* analyze = new QPushButton{"Analyze WAV with ODAS", page};
    auto* play = new QPushButton{"Pause replay", page};
    auto* stop = new QPushButton{"Reset view data", page};
    play->setEnabled(false);
#if !LIBAUDITION_DEMO_HAS_ODAS
    analyze->setEnabled(false);
    analyze->setToolTip("Reconfigure with LIBAUDITION_WITH_ODAS=ON");
#endif
    actions->addWidget(example);
    actions->addWidget(analyze);
    actions->addWidget(play);
    actions->addWidget(stop);
    actions->addStretch();
    layout->addLayout(actions);

    auto* status = new QLabel{
#if LIBAUDITION_DEMO_HAS_ODAS
        "Ready. Select WAV or show the explicitly synthetic 3D example.", page
#else
        "ODAS not linked. Rebuild with LIBAUDITION_WITH_ODAS=ON to analyze WAV.", page
#endif
    };
    status->setObjectName("acousticSceneStatus");
    status->setWordWrap(true);
    layout->addWidget(status);
    auto* diagnostics = new QLabel{
        "Live signal diagnostics will show channel RMS/peak levels and "
        "SSL proposal counts after capture starts.", page};
    diagnostics->setObjectName("acousticSceneDiagnostics");
    diagnostics->setWordWrap(true);
    diagnostics->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(diagnostics);

    auto* split = new QSplitter{Qt::Horizontal, page};
    auto* sphere = new AcousticSphereWidget{split};
    split->addWidget(sphere);
    auto* sidebar = new QWidget{split};
    auto* sideLayout = new QVBoxLayout{sidebar};
    auto* tracks = new QTableWidget{0, 4, sidebar};
    tracks->setObjectName("acousticSceneTracks");
    tracks->setHorizontalHeaderLabels({"ID", "Azimuth", "Elevation", "Activity"});
    tracks->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    tracks->verticalHeader()->hide();
    tracks->setEditTriggers(QAbstractItemView::NoEditTriggers);
    sideLayout->addWidget(new QLabel{"Tracked sources", sidebar});
    sideLayout->addWidget(tracks);
    auto* selected = new QLabel{"Click a source on the sphere to inspect it.", sidebar};
    selected->setObjectName("acousticSceneSelected");
    selected->setWordWrap(true);
    selected->setMinimumHeight(115);
    sideLayout->addWidget(selected);
    sideLayout->addWidget(new QLabel{
        "Orange grid: below equator\nBlue grid: above equator\n"
        "XYZ axes: +X forward / +Y left / +Z up\n"
        "Hover: azimuth, elevation, activity\n"
        "Classification/ASR are not connected in this viewer yet.", sidebar});
    split->addWidget(sidebar);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    layout->addWidget(split, 1);

    auto replay = std::make_shared<SceneReplay>();
    auto cursor = std::make_shared<std::size_t>(0);
    auto* timer = new QTimer{page};
    timer->setInterval(50);
    auto display = [=](const std::vector<AcousticSceneTrack>& values) {
        sphere->setTracks(values);
        tracks->setRowCount(static_cast<int>(values.size()));
        for (int i = 0; i < static_cast<int>(values.size()); ++i) {
            const auto& track = values[static_cast<std::size_t>(i)];
            const auto v = track.direction;
            const double az = std::atan2(v.y(), v.x()) * 180.0 / kPi;
            const double el = std::atan2(v.z(), std::hypot(v.x(), v.y())) * 180.0 / kPi;
            for (int column = 0; column < 4; ++column) {
                QString data;
                switch (column) {
                case 0: data = QString::number(static_cast<qulonglong>(track.id)); break;
                case 1: data = QString::number(az, 'f', 1) + "°"; break;
                case 2: data = QString::number(el, 'f', 1) + "°"; break;
                default: data = QString::number(track.activity, 'f', 2); break;
                }
                tracks->setItem(i, column, new QTableWidgetItem{data});
            }
            if (track.id == sphere->selectedTrack()) {
                selected->setText(directionSummary(track));
            }
        }
    };
    auto capture = std::make_shared<demo::AlsaLiveCapture>();
    auto* liveTimer = new QTimer{page};
    liveTimer->setInterval(50);
    auto seenGeneration = std::make_shared<std::uint64_t>(0);

    const auto stopCapture = [=]() {
        liveTimer->stop();
        capture->stop(); // joins safely before the widget can be destroyed
        stopLive->setEnabled(false);
        startLive->setEnabled(
#if LIBAUDITION_DEMO_HAS_ALSA
            true
#else
            false
#endif
        );
        refreshDevices->setEnabled(
#if LIBAUDITION_DEMO_HAS_ALSA
            true
#else
            false
#endif
        );
        deviceSelector->setEnabled(true);
        captureChannels->setEnabled(true);
        captureRate->setEnabled(true);
        map->setEnabled(true);
        geometry->setEnabled(true);
        hop->setEnabled(true);
        frame->setEnabled(true);
        example->setEnabled(true);
        analyze->setEnabled(
#if LIBAUDITION_DEMO_HAS_ODAS
            true
#else
            false
#endif
        );
    };
    // A background thread may still be using ALSA when the window closes.
    QObject::connect(page, &QObject::destroyed, [capture]() {
        capture->stop();
    });
    QObject::connect(stopLive, &QPushButton::clicked, page, [=]() {
        stopCapture();
        status->setText("Live capture stopped. Last scene is retained.");
    });

#if LIBAUDITION_DEMO_HAS_ALSA
    const auto refresh = [=]() {
        const auto previous = deviceSelector->currentData().toString();
        deviceSelector->clear();
        const auto devices = demo::AlsaLiveCapture::discover();
        int selectedIndex = -1;
        for (const auto& device : devices) {
            const auto pcm = QString::fromStdString(device.pcm_name);
            const QString caption = pcm + " | " +
                QString::fromStdString(device.description) +
                (device.likely_respeaker ? " [ReSpeaker candidate]" : "");
            deviceSelector->addItem(caption, pcm);
            if (device.likely_respeaker && selectedIndex < 0) {
                selectedIndex = deviceSelector->count() - 1;
            }
            if (pcm == previous) {
                selectedIndex = deviceSelector->count() - 1;
            }
        }
        if (selectedIndex >= 0) {
            deviceSelector->setCurrentIndex(selectedIndex);
        }
        deviceStatus->setText(
            devices.empty()
            ? "No ALSA hardware capture devices detected. "
              "Enter hw:N,M manually or check arecord -l."
            : QString("Detected %1 capture PCM(s). ReSpeaker candidates are "
                      "prioritized; confirm the hardware and microphone order.")
                  .arg(static_cast<qulonglong>(devices.size())));
    };
    QObject::connect(refreshDevices, &QPushButton::clicked, page, refresh);
    refresh();

    QObject::connect(startLive, &QPushButton::clicked, page, [=]() {
        try {
            if (!analyze->isEnabled()) {
                throw std::runtime_error{
                    "Wait for WAV analysis to finish before starting live capture"};
            }
            demo::LiveCaptureConfig config;
            QString pcmName = deviceSelector->currentData().toString();
            const auto chosenText = deviceSelector->currentText().trimmed();
            if (pcmName.isEmpty() || !chosenText.startsWith(pcmName)) {
                pcmName = chosenText.section(" | ", 0, 0);
            }
            config.pcm_name = pcmName.toStdString();
            config.channel_count =
                static_cast<std::uint32_t>(captureChannels->value());
            config.sample_rate_hz =
                static_cast<std::uint32_t>(captureRate->value());
            config.hop_size = static_cast<std::uint32_t>(hop->value());
            config.frame_size = static_cast<std::uint32_t>(frame->value());
            config.input_channels = parseChannels(map->text());
            const auto microphoneGeometry = parseMicrophones(geometry->toPlainText());
            for (const auto& microphone : microphoneGeometry) {
                const auto& v = microphone.position_m;
                config.microphone_positions.push_back({v.x, v.y, v.z});
            }
            if (config.input_channels.size() != config.microphone_positions.size()) {
                throw std::runtime_error{
                    "ODAS channel mapping length must match the microphone XYZ count"};
            }
            for (const auto index : config.input_channels) {
                if (index >= config.channel_count) {
                    throw std::runtime_error{
                        "ODAS input channel index is outside the capture channel count"};
                }
            }
            timer->stop();
            play->setEnabled(false);
            *cursor = 0;
            replay->frames.clear();
            sphere->clearTracks();
            tracks->setRowCount(0);
            selected->setText("Click a live track to inspect its direction.");
            capture->start(std::move(config));
            *seenGeneration = 0;
            startLive->setEnabled(false);
            stopLive->setEnabled(true);
            refreshDevices->setEnabled(false);
            deviceSelector->setEnabled(false);
            captureChannels->setEnabled(false);
            captureRate->setEnabled(false);
            map->setEnabled(false);
            geometry->setEnabled(false);
            hop->setEnabled(false);
            frame->setEnabled(false);
            example->setEnabled(false);
            analyze->setEnabled(false);
            status->setText("Opening ALSA capture stream and initializing ODAS…");
            diagnostics->setText("Waiting for first ALSA audio frames…");
            liveTimer->start();
        } catch (const std::exception& error) {
            status->setText(QString("Live capture configuration error: ") +
                            QString::fromUtf8(error.what()));
        }
    });
#endif

    QObject::connect(liveTimer, &QTimer::timeout, page, [=]() {
        const auto snapshot = capture->snapshot();
        if (snapshot.generation != *seenGeneration) {
            *seenGeneration = snapshot.generation;
            display(snapshot.tracks);
            sphere->setPotentials(snapshot.potentials);
            QStringList levels;
            for (std::size_t channel = 0; channel < snapshot.channel_rms_dbfs.size();
                 ++channel) {
                levels << QString("ch%1: %2 dBFS RMS / %3 peak")
                              .arg(static_cast<qulonglong>(channel))
                              .arg(snapshot.channel_rms_dbfs[channel], 0, 'f', 1)
                              .arg(snapshot.channel_peak_dbfs[channel], 0, 'f', 1);
            }
            double strongest = 0.0;
            for (const auto& proposal : snapshot.potentials) {
                strongest = std::max(strongest, proposal.score);
            }
            diagnostics->setText(
                QString("Raw ALSA input levels: %1\n"
                        "ODAS SSL direction proposals: %2 (strongest raw score: %3) · "
                        "outlined diamonds are SSL proposals, filled circles are SST tracks")
                    .arg(levels.join("  |  "))
                    .arg(static_cast<qulonglong>(snapshot.potentials.size()))
                    .arg(strongest, 0, 'f', 3));
        }
        if (!snapshot.error.empty()) {
            stopCapture();
            status->setText("Live ALSA/ODAS error: " +
                            QString::fromStdString(snapshot.error));
            return;
        }
        if (!snapshot.running) {
            stopCapture();
            status->setText("Live capture completed or device disconnected.");
            return;
        }
        status->setText(
            QString("LIVE ALSA → ODAS · %1 hops · %2 tracked source(s) · "
                    "ALSA overrun recoveries: %3 · UI 20 Hz")
                .arg(static_cast<qulonglong>(snapshot.processed_hops))
                .arg(static_cast<qulonglong>(snapshot.tracks.size()))
                .arg(static_cast<qulonglong>(snapshot.recoveries)));
    });

    sphere->setTrackClicked([=](std::uint64_t id) {
        const auto current = capture->snapshot();
        if (current.running) {
            for (const auto& track : current.tracks) {
                if (track.id == id) {
                    selected->setText(directionSummary(track));
                    return;
                }
            }
        }
        if (*cursor > 0 && *cursor <= replay->frames.size()) {
            for (const auto& track : replay->frames[*cursor - 1].tracks) {
                if (track.id == id) {
                    selected->setText(directionSummary(track));
                    return;
                }
            }
        }
        selected->setText(QString("Track #%1 selected")
                              .arg(static_cast<qulonglong>(id)));
    });
    QObject::connect(timer, &QTimer::timeout, page, [=]() {
        if (*cursor >= replay->frames.size()) {
            timer->stop();
            play->setEnabled(false);
            status->setText("Replay complete (tracks only; WAV audio not played).");
            return;
        }
        const auto& current = replay->frames[*cursor];
        display(current.tracks);
        status->setText(QString("ODAS direction replay: %1 s · frame %2/%3")
            .arg(current.seconds, 0, 'f', 2)
            .arg(static_cast<qulonglong>(*cursor + 1))
            .arg(static_cast<qulonglong>(replay->frames.size())));
        ++*cursor;
    });

    QObject::connect(example, &QPushButton::clicked, page, [=]() {
        stopCapture();
        timer->stop();
        replay->frames.clear();
        *cursor = 0;
        play->setEnabled(false);
        sphere->clearTracks();
        diagnostics->setText("Synthetic example (no live microphone levels).");
        const std::vector<AcousticSceneTrack> sample{
            {12, QVector3D{0.71F, 0.46F, 0.53F}, 0.92},
            {7, QVector3D{0.42F, -0.57F, -0.71F}, 0.61},
            {25, QVector3D{-0.62F, 0.38F, 0.68F}, 0.34}};
        display(sample);
        status->setText("SYNTHETIC 3D EXAMPLE — not an ODAS measurement. "
                        "Drag the sphere; observe above/below equator.");
    });

    QObject::connect(play, &QPushButton::clicked, page, [=]() {
        if (timer->isActive()) {
            timer->stop();
            play->setText("Resume replay");
        } else if (*cursor < replay->frames.size()) {
            timer->start();
            play->setText("Pause replay");
        }
    });
    QObject::connect(stop, &QPushButton::clicked, page, [=]() {
        stopCapture();
        timer->stop();
        *cursor = 0;
        replay->frames.clear();
        sphere->clearTracks();
        tracks->setRowCount(0);
        selected->setText("Click a source on the sphere to inspect it.");
        play->setEnabled(false);
        status->setText("View data reset.");
        diagnostics->setText("No live microphone diagnostics.");
    });

#if LIBAUDITION_DEMO_HAS_ODAS
    auto* watcher = new QFutureWatcher<std::shared_ptr<SceneReplay>>{page};
    QObject::connect(analyze, &QPushButton::clicked, page, [=]() {
        stopCapture();
        if (watcher->isRunning()) {
            return;
        }
        const std::filesystem::path wavFile{wavPath->text().toStdString()};
        const auto mapping = map->text();
        const auto positions = geometry->toPlainText();
        const auto hopSize = static_cast<std::uint32_t>(hop->value());
        const auto frameSize = static_cast<std::uint32_t>(frame->value());
        timer->stop();
        play->setEnabled(false);
        analyze->setEnabled(false);
        status->setText("ODAS is analyzing WAV in a worker. Please wait…");
        diagnostics->setText("WAV replay mode; live ALSA levels not available.");
        watcher->setFuture(QtConcurrent::run([=]() {
            return analyzeWav(wavFile, mapping, positions, hopSize, frameSize);
        }));
    });
    QObject::connect(watcher, &QFutureWatcherBase::finished, page, [=]() {
        analyze->setEnabled(true);
        const auto computed = watcher->result();
        if (!computed->error.isEmpty()) {
            status->setText("ODAS error: " + computed->error);
            return;
        }
        *replay = *computed;
        *cursor = 0;
        sphere->clearTracks();
        tracks->setRowCount(0);
        if (replay->frames.empty()) {
            status->setText("No complete ODAS frames.");
            return;
        }
        play->setEnabled(true);
        play->setText("Pause replay");
        timer->start();
        status->setText(QString("Analyzed %1 ODAS hops; replaying %2 snapshots.")
            .arg(static_cast<qulonglong>(computed->hops))
            .arg(static_cast<qulonglong>(computed->frames.size())));
    });
#endif
    return page;
}
