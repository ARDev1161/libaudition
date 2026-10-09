#include "sherpa_speaker_workbench.hpp"

#include "wav_io.hpp"

#include <QCheckBox>
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

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <optional>
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
    const QString& filter = QStringLiteral("All files (*)")) {
    auto* row = new QWidget{parent};
    auto* layout = new QHBoxLayout{row};
    layout->setContentsMargins(0, 0, 0, 0);

    edit = new QLineEdit{row};
    auto* choose = new QPushButton{QStringLiteral("Browse…"), row};
    layout->addWidget(edit, 1);
    layout->addWidget(choose);

    QObject::connect(
        choose,
        &QPushButton::clicked,
        row,
        [edit, filter]() {
            const auto path = QFileDialog::getOpenFileName(
                nullptr,
                QStringLiteral("Choose file"),
                edit->text(),
                filter);
            if (!path.isEmpty()) {
                edit->setText(path);
            }
        });

    return row;
}

std::filesystem::path fsPath(const QLineEdit* edit) {
    return std::filesystem::path{edit->text().toStdString()};
}

audition::AudioBuffer loadMonoPath(
    const std::filesystem::path& path,
    std::size_t channel) {
    const auto wav = demo::loadWav(path);
    return demo::selectMonoChannel(wav.audio.view(), channel);
}

audition::AudioBuffer loadMono(
    const QLineEdit* wavPath,
    const QSpinBox* channel) {
    if (wavPath->text().isEmpty()) {
        throw std::runtime_error{"WAV path is required"};
    }
    return loadMonoPath(
        fsPath(wavPath),
        static_cast<std::size_t>(channel->value()));
}

QString audioSummary(const audition::AudioBuffer& audio) {
    std::ostringstream text;
    text << "sample_rate_hz=" << audio.format().sample_rate_hz
         << " frames=" << audio.frameCount()
         << " duration_s=" << audio.duration().seconds();
    return QString::fromStdString(text.str());
}

audition::SherpaSpeakerEmbeddingOptions embeddingOptions(
    const QLineEdit* model,
    const QSpinBox* sampleRate,
    const QLineEdit* modelId) {
    audition::SherpaSpeakerEmbeddingOptions options{};
    options.model = fsPath(model);
    options.sample_rate_hz =
        static_cast<std::uint32_t>(sampleRate->value());
    options.model_id = modelId->text().trimmed().toStdString();
    return options;
}

audition::SpeakerEmbedding requireEmbedding(
    const audition::SherpaSpeakerEmbedder& embedder,
    audition::AudioView audio,
    const char* role) {
    auto result = embedder.embed(audio);
    if (!result.has_value()) {
        throw std::runtime_error{
            std::string{role} +
            " is too short for the selected speaker embedding model"};
    }
    return std::move(*result);
}

double l2Norm(const std::vector<float>& values) {
    double norm2 = 0.0;
    for (const float value : values) {
        norm2 += static_cast<double>(value) *
                 static_cast<double>(value);
    }
    return std::sqrt(norm2);
}

void addEmbeddingModelRows(
    QFormLayout* form,
    QWidget* page,
    QLineEdit*& model,
    QSpinBox*& sampleRate,
    QLineEdit*& modelId) {
    form->addRow(
        "Speaker embedding model",
        pathEditor(
            model,
            page,
            "ONNX model (*.onnx);;All files (*)"));
    sampleRate = intBox(8000, 192000, 16000, 1000);
    form->addRow("Model sample rate", sampleRate);
    modelId = new QLineEdit{page};
    modelId->setPlaceholderText(
        "optional; defaults to model filename");
    form->addRow("Model ID", modelId);
}

QWidget* createSpeakerEmbeddingPanel(QWidget* parent) {
    auto* page = new QWidget{parent};
    auto* layout = new QVBoxLayout{page};

    layout->addWidget(description(
        "Extracts a real Sherpa speaker embedding from one WAV channel. "
        "The model sample rate is explicit and the GUI does not resample. "
        "If the clip is too short for the model, the public embed() API "
        "returns no embedding and the panel reports that condition."));

    auto* form = new QFormLayout;
    QLineEdit* wavPath = nullptr;
    QLineEdit* model = nullptr;
    QLineEdit* modelId = nullptr;
    QSpinBox* sampleRate = nullptr;
    auto* channel = intBox(0, 255, 0);

    form->addRow(
        "Input WAV",
        pathEditor(
            wavPath,
            page,
            "WAV audio (*.wav *.WAV);;All files (*)"));
    form->addRow("Channel (0-based)", channel);
    addEmbeddingModelRows(
        form,
        page,
        model,
        sampleRate,
        modelId);
    layout->addLayout(form);

    auto* run = new QPushButton{"Extract speaker embedding"};
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
                audition::SherpaSpeakerEmbedder embedder{
                    embeddingOptions(model, sampleRate, modelId)};
                const auto embedding = requireEmbedding(
                    embedder,
                    mono.view(),
                    "Input WAV");

                std::ostringstream text;
                text << "backend=" << embedder.backendInfo().name
                     << "\ninput=" << audioSummary(mono).toStdString()
                     << "\nmodel_id=" << embedding.model_id
                     << "\ndimension=" << embedding.values.size()
                     << "\nreported_dimension="
                     << embedder.embeddingDimension()
                     << "\nl2_norm=" << l2Norm(embedding.values)
                     << "\nquality=";
                if (embedding.quality.has_value()) {
                    text << embedding.quality->value();
                } else {
                    text << "<absent>";
                }

                text << "\npreview=[";
                const std::size_t preview =
                    std::min<std::size_t>(12U, embedding.values.size());
                for (std::size_t index = 0U;
                     index < preview;
                     ++index) {
                    if (index != 0U) {
                        text << ", ";
                    }
                    text << std::setprecision(6)
                         << embedding.values[index];
                }
                if (embedding.values.size() > preview) {
                    text << ", ...";
                }
                text << "]";

                output->setPlainText(
                    QString::fromStdString(text.str()));
            } catch (const std::exception& error) {
                output->setPlainText(exceptionText(error));
            }
        });

    return page;
}

QWidget* createSpeakerVerificationPanel(QWidget* parent) {
    auto* page = new QWidget{parent};
    auto* layout = new QVBoxLayout{page};

    layout->addWidget(description(
        "End-to-end speaker verification: the same Sherpa embedding model "
        "extracts vectors from reference and candidate WAVs, then "
        "SherpaSpeakerVerifier compares them against a cosine-similarity "
        "threshold in [-1, 1]."));

    auto* form = new QFormLayout;
    QLineEdit* referencePath = nullptr;
    QLineEdit* candidatePath = nullptr;
    QLineEdit* model = nullptr;
    QLineEdit* modelId = nullptr;
    QSpinBox* sampleRate = nullptr;
    auto* referenceChannel = intBox(0, 255, 0);
    auto* candidateChannel = intBox(0, 255, 0);
    auto* threshold = doubleBox(-1.0, 1.0, 0.5, 3, 0.05);

    form->addRow(
        "Reference WAV",
        pathEditor(
            referencePath,
            page,
            "WAV audio (*.wav *.WAV);;All files (*)"));
    form->addRow("Reference channel", referenceChannel);
    form->addRow(
        "Candidate WAV",
        pathEditor(
            candidatePath,
            page,
            "WAV audio (*.wav *.WAV);;All files (*)"));
    form->addRow("Candidate channel", candidateChannel);
    addEmbeddingModelRows(
        form,
        page,
        model,
        sampleRate,
        modelId);
    form->addRow("Similarity threshold", threshold);
    layout->addLayout(form);

    auto* run = new QPushButton{"Verify speaker"};
    auto* output = outputBox();
    layout->addWidget(run);
    layout->addWidget(output);

    QObject::connect(
        run,
        &QPushButton::clicked,
        page,
        [=]() {
            try {
                auto reference =
                    loadMono(referencePath, referenceChannel);
                auto candidate =
                    loadMono(candidatePath, candidateChannel);

                audition::SherpaSpeakerEmbedder embedder{
                    embeddingOptions(model, sampleRate, modelId)};
                const auto referenceEmbedding =
                    requireEmbedding(
                        embedder,
                        reference.view(),
                        "Reference WAV");
                const auto candidateEmbedding =
                    requireEmbedding(
                        embedder,
                        candidate.view(),
                        "Candidate WAV");

                audition::SherpaSpeakerVerifier verifier{};
                const auto result = verifier.compare(
                    referenceEmbedding,
                    candidateEmbedding,
                    audition::Score{threshold->value()});

                std::ostringstream text;
                text << "embedder_backend="
                     << embedder.backendInfo().name
                     << "\nverifier_backend="
                     << verifier.backendInfo().name
                     << "\nreference="
                     << audioSummary(reference).toStdString()
                     << "\ncandidate="
                     << audioSummary(candidate).toStdString()
                     << "\nmodel_id="
                     << referenceEmbedding.model_id
                     << "\ndimension="
                     << referenceEmbedding.values.size()
                     << "\nsimilarity="
                     << result.similarity.value
                     << "\nthreshold="
                     << threshold->value()
                     << "\nmatched="
                     << (result.matched ? "yes" : "no")
                     << "\ncalibrated_probability=";
                if (result.calibrated_probability.has_value()) {
                    text << result.calibrated_probability->value();
                } else {
                    text << "<absent>";
                }

                output->setPlainText(
                    QString::fromStdString(text.str()));
            } catch (const std::exception& error) {
                output->setPlainText(exceptionText(error));
            }
        });

    return page;
}

struct EnrollmentInput {
    std::string display_name{};
    std::vector<audition::SpeakerEmbedding> embeddings{};
};

std::vector<EnrollmentInput> buildEnrollments(
    const QString& specification,
    const audition::SherpaSpeakerEmbedder& embedder) {
    std::vector<EnrollmentInput> result;

    for (const auto& rawLine :
         specification.split('\n', Qt::SkipEmptyParts)) {
        const auto line = rawLine.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const auto fields = line.split('|');
        if (fields.size() < 2 || fields.size() > 3) {
            throw std::runtime_error{
                "Each enrollment line must be name|wav_path|channel"};
        }

        const auto name = fields[0].trimmed().toStdString();
        const auto path = fields[1].trimmed().toStdString();
        if (name.empty() || path.empty()) {
            throw std::runtime_error{
                "Enrollment name and WAV path must be non-empty"};
        }

        std::size_t channel = 0U;
        if (fields.size() == 3) {
            bool ok = false;
            const auto parsed =
                fields[2].trimmed().toULongLong(&ok);
            if (!ok) {
                throw std::runtime_error{
                    "Enrollment channel must be a non-negative integer"};
            }
            channel = static_cast<std::size_t>(parsed);
        }

        auto audio = loadMonoPath(
            std::filesystem::path{path},
            channel);
        auto embedding = requireEmbedding(
            embedder,
            audio.view(),
            "Enrollment WAV");

        auto existing = std::find_if(
            result.begin(),
            result.end(),
            [&](const EnrollmentInput& value) {
                return value.display_name == name;
            });
        if (existing == result.end()) {
            EnrollmentInput entry{};
            entry.display_name = name;
            entry.embeddings.push_back(std::move(embedding));
            result.push_back(std::move(entry));
        } else {
            existing->embeddings.push_back(std::move(embedding));
        }
    }

    if (result.empty()) {
        throw std::runtime_error{
            "At least one enrollment line is required"};
    }
    return result;
}

QWidget* createSpeakerIdentificationPanel(QWidget* parent) {
    auto* page = new QWidget{parent};
    auto* layout = new QVBoxLayout{page};

    layout->addWidget(description(
        "Builds a temporary in-memory speaker index from WAV files, then "
        "identifies a query WAV. Enrollment syntax is one line per sample: "
        "name|wav_path|channel. Repeating the same name adds another "
        "embedding to that speaker. Nothing is persisted by the GUI."));

    auto* form = new QFormLayout;
    QLineEdit* queryPath = nullptr;
    QLineEdit* model = nullptr;
    QLineEdit* modelId = nullptr;
    QSpinBox* sampleRate = nullptr;
    auto* queryChannel = intBox(0, 255, 0);
    auto* threshold = doubleBox(-1.0, 1.0, 0.5, 3, 0.05);
    auto* topK = intBox(1, 1000, 5);
    auto* enrollmentText = new QPlainTextEdit{page};
    enrollmentText->setPlaceholderText(
        "Alice|/path/alice1.wav|0\n"
        "Alice|/path/alice2.wav|0\n"
        "Bob|/path/bob.wav|0");
    enrollmentText->setMinimumHeight(110);

    form->addRow("Enrollments", enrollmentText);
    form->addRow(
        "Query WAV",
        pathEditor(
            queryPath,
            page,
            "WAV audio (*.wav *.WAV);;All files (*)"));
    form->addRow("Query channel", queryChannel);
    addEmbeddingModelRows(
        form,
        page,
        model,
        sampleRate,
        modelId);
    form->addRow("Similarity threshold", threshold);
    form->addRow("Top K", topK);
    layout->addLayout(form);

    auto* run = new QPushButton{"Build index and identify"};
    auto* output = outputBox();
    layout->addWidget(run);
    layout->addWidget(output);

    QObject::connect(
        run,
        &QPushButton::clicked,
        page,
        [=]() {
            try {
                audition::SherpaSpeakerEmbedder embedder{
                    embeddingOptions(model, sampleRate, modelId)};
                auto enrollments = buildEnrollments(
                    enrollmentText->toPlainText(),
                    embedder);

                auto query =
                    loadMono(queryPath, queryChannel);
                const auto queryEmbedding =
                    requireEmbedding(
                        embedder,
                        query.view(),
                        "Query WAV");

                audition::SherpaSpeakerIdentifierOptions identifierOptions{};
                identifierOptions.embedding_dimension =
                    embedder.embeddingDimension();
                identifierOptions.model_id =
                    queryEmbedding.model_id;
                audition::SherpaSpeakerIdentifier identifier{
                    identifierOptions};

                std::size_t enrolledEmbeddings = 0U;
                for (std::size_t index = 0U;
                     index < enrollments.size();
                     ++index) {
                    audition::SpeakerEnrollment enrollment{};
                    enrollment.speaker_id =
                        audition::SpeakerId{
                            static_cast<std::uint64_t>(index + 1U)};
                    enrollment.display_name =
                        enrollments[index].display_name;
                    enrollment.embeddings =
                        std::move(enrollments[index].embeddings);
                    enrolledEmbeddings +=
                        enrollment.embeddings.size();
                    identifier.enroll(enrollment);
                }

                const auto matches = identifier.identifyTopK(
                    queryEmbedding,
                    audition::Score{threshold->value()},
                    static_cast<std::size_t>(topK->value()));

                std::ostringstream text;
                text << "embedder_backend="
                     << embedder.backendInfo().name
                     << "\nidentifier_backend="
                     << identifier.backendInfo().name
                     << "\nquery="
                     << audioSummary(query).toStdString()
                     << "\nmodel_id="
                     << queryEmbedding.model_id
                     << "\ndimension="
                     << queryEmbedding.values.size()
                     << "\nenrolled_speakers="
                     << enrollments.size()
                     << "\nenrolled_embeddings="
                     << enrolledEmbeddings
                     << "\nthreshold="
                     << threshold->value()
                     << "\nmatches="
                     << matches.size()
                     << "\n\n";

                for (const auto& match : matches) {
                    text << "speaker_id="
                         << match.speaker_id.value()
                         << " name="
                         << match.display_name
                         << " similarity="
                         << match.similarity.value
                         << "\n";
                }

                output->setPlainText(
                    QString::fromStdString(text.str()));
            } catch (const std::exception& error) {
                output->setPlainText(exceptionText(error));
            }
        });

    return page;
}

QWidget* createSpeakerDiarizationPanel(QWidget* parent) {
    auto* page = new QWidget{parent};
    auto* layout = new QVBoxLayout{page};

    layout->addWidget(description(
        "Runs SherpaSpeakerDiarizer over one WAV channel using a pyannote "
        "segmentation model plus a speaker embedding model. The backend "
        "discovers the model sample rate and rejects mismatched WAV input; "
        "the GUI never resamples it."));

    auto* form = new QFormLayout;
    QLineEdit* wavPath = nullptr;
    QLineEdit* segmentationModel = nullptr;
    QLineEdit* embeddingModel = nullptr;
    auto* channel = intBox(0, 255, 0);
    auto* numClusters = intBox(0, 1000, 0);
    auto* clusteringThreshold =
        doubleBox(0.001, 100.0, 0.5, 3, 0.05);
    auto* windowShift =
        doubleBox(0.001, 1.0, 0.1, 3, 0.01);
    auto* minDurationOn =
        doubleBox(0.0, 3600.0, 0.0, 3, 0.1);
    auto* minDurationOff =
        doubleBox(0.0, 3600.0, 0.0, 3, 0.1);
    auto* confidence =
        new QCheckBox{"Compute segment confidence"};

    form->addRow(
        "Input WAV",
        pathEditor(
            wavPath,
            page,
            "WAV audio (*.wav *.WAV);;All files (*)"));
    form->addRow("Channel (0-based)", channel);
    form->addRow(
        "Segmentation model",
        pathEditor(
            segmentationModel,
            page,
            "ONNX model (*.onnx);;All files (*)"));
    form->addRow(
        "Embedding model",
        pathEditor(
            embeddingModel,
            page,
            "ONNX model (*.onnx);;All files (*)"));
    form->addRow(
        "Window shift ratio",
        windowShift);
    form->addRow(
        "Clusters (0 = automatic)",
        numClusters);
    form->addRow(
        "Clustering threshold",
        clusteringThreshold);
    form->addRow(
        "Min duration on [s]",
        minDurationOn);
    form->addRow(
        "Min duration off [s]",
        minDurationOff);
    form->addRow(confidence);
    layout->addLayout(form);

    auto* run = new QPushButton{"Run speaker diarization"};
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

                audition::SherpaSpeakerDiarizationOptions options{};
                options.segmentation_model =
                    fsPath(segmentationModel);
                options.embedding_model =
                    fsPath(embeddingModel);
                options.segmentation_window_shift_ratio =
                    static_cast<float>(windowShift->value());
                options.num_clusters =
                    numClusters->value();
                options.clustering_threshold =
                    static_cast<float>(
                        clusteringThreshold->value());
                options.compute_confidence =
                    confidence->isChecked();
                options.min_duration_on =
                    static_cast<float>(minDurationOn->value());
                options.min_duration_off =
                    static_cast<float>(minDurationOff->value());

                audition::SherpaSpeakerDiarizer diarizer{
                    options};
                const auto result =
                    diarizer.diarize(mono.view());

                std::ostringstream text;
                text << "backend="
                     << diarizer.backendInfo().name
                     << "\ninput="
                     << audioSummary(mono).toStdString()
                     << "\nspeaker_count="
                     << result.speaker_count
                     << "\nsegments="
                     << result.segments.size()
                     << "\n\n";

                for (const auto& segment :
                     result.segments) {
                    text << "speaker="
                         << segment.speaker_index
                         << " start_s="
                         << segment.start_offset.seconds()
                         << " end_s="
                         << segment.end_offset.seconds()
                         << " confidence=";
                    if (segment.confidence.has_value()) {
                        text << segment.confidence->value;
                    } else {
                        text << "<absent>";
                    }
                    text << "\n";
                }

                output->setPlainText(
                    QString::fromStdString(text.str()));
            } catch (const std::exception& error) {
                output->setPlainText(exceptionText(error));
            }
        });

    return page;
}

#endif

}  // namespace

void addSherpaSpeakerWorkbenchTabs(QTabWidget* tabs) {
#if LIBAUDITION_DEMO_HAS_SHERPA
    tabs->addTab(
        createSpeakerEmbeddingPanel(tabs),
        "Speaker embedding");
    tabs->addTab(
        createSpeakerVerificationPanel(tabs),
        "Speaker verification");
    tabs->addTab(
        createSpeakerIdentificationPanel(tabs),
        "Speaker identification");
    tabs->addTab(
        createSpeakerDiarizationPanel(tabs),
        "Diarization");
#else
    static_cast<void>(tabs);
#endif
}
