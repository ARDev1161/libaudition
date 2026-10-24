#include "feature_catalog_page.hpp"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace {

struct Feature {
    const char* group;
    const char* capability;
    const char* tab;
    const char* access;
};

constexpr Feature kFeatures[] = {
    {"Core / DSP", "PCM buffers, time, layouts and I/O", "Audio / DSP", "Interactive + API"},
    {"Core / DSP", "Waveform quality, RMS, peak and clipping", "Audio / DSP", "Interactive + API"},
    {"Core / DSP", "Audio frontend, filters and feature extraction", "Audio / DSP", "Interactive + API"},
    {"Core / DSP", "WAV inspect and channel selection", "Backend workbench", "Interactive"},
    {"Core / DSP", "Sampling-rate conversion", "Backend workbench", "Optional backend"},
    {"Core / DSP", "Acoustic echo cancellation (AEC3)", "Backend workbench", "Optional backend"},
    {"Spatial", "Sound pressure calibration (dBFS / SPL)", "SPL / Range", "Interactive + CLI"},
    {"Spatial", "Acoustic distance and range priors", "SPL / Range", "Interactive + CLI"},
    {"Spatial", "Range and position temporal smoothing", "Smoothing", "Interactive + CLI"},
    {"Spatial", "3D tracking and separated source audio (ODAS)", "Acoustic scene", "Optional backend + live capture"},
    {"Spatial", "Source fingerprints and persistent identity", "Source identity", "Interactive + CLI"},
    {"Spatial", "Spatial fusion / uncertainty (GTSAM)", "Backend workbench", "Optional backend"},
    {"Spatial", "Spatial identity coordinator", "Source identity", "API + CTest"},
    {"Acoustic events", "AcousticEvent lifecycle and annotation", "AcousticEvent", "Interactive + CLI"},
    {"Acoustic events", "Temporal event detection with hysteresis", "Temporal patterns", "Interactive + CLI"},
    {"Temporal", "Scalar EMA/CUSUM change detection", "Temporal patterns", "Interactive + CLI"},
    {"Temporal", "Timed activity segmentation and interval relations", "Temporal patterns", "API + CLI"},
    {"Temporal", "Event association, gating and Hungarian matching", "Temporal patterns", "Interactive matching + CLI"},
    {"Temporal", "Periodicity / recurrence of event onsets", "Temporal patterns", "Interactive + CLI"},
    {"Temporal", "Dynamic time warping (DTW)", "Temporal patterns", "Interactive + CLI"},
    {"Temporal", "Offline repeated acoustic motifs", "Temporal patterns", "Interactive + CLI"},
    {"Temporal", "Per-source streaming pattern detection", "Temporal patterns", "Interactive + CLI"},
    {"Classification", "YAMNet and Sherpa acoustic event tagging", "Acoustic scene", "Optional backend + model"},
    {"Classification", "Progressive per-source classification runtime", "Acoustic scene", "Optional backend + model"},
    {"Classification", "EfficientAT and audio embeddings", "Backend workbench", "Optional backend + model"},
    {"Classification", "Open-vocabulary text/audio CLAP", "Backend workbench", "Optional backend + model"},
    {"Speech", "Per-source VAD, pre/post-roll and ASR", "Acoustic scene", "Optional backend + model"},
    {"Speech", "Bounded PCM ingress, epochs and cancellation", "Acoustic scene", "API + CTest; integrated capture"},
    {"Speech", "Offline/online ASR and keyword spotting", "Backend workbench", "Optional backend + model"},
    {"Speech", "Language identification and denoising", "Backend workbench", "Optional backend + model"},
    {"Speech", "Speaker embeddings, verification and ID", "Backend workbench", "Optional backend + model"},
    {"Speech", "Diarization and speaker-turn segmentation", "Backend workbench", "Optional backend + model"},
    {"Speech", "Speech synthesis via VITS / Piper", "Backend workbench", "Optional backend + model"},
    {"Analysis", "Voice traits / WORLD analysis", "Backend workbench", "Optional backend"},
    {"Analysis", "Synthetic voice detection / AASIST", "Backend workbench", "Optional backend + model"},
    {"Memory / model", "Speaker/source/sound prototype registries", "Source identity", "API + CTest"},
    {"Memory / model", "Model metadata, execution and backend factories", "Backends", "API + CTest"},
    {"Integration", "End-to-end routed acoustic pipeline", "Pipeline", "Interactive + CLI"},
    {"Integration", "WAV/JSON recording timeline and sidecars", "Acoustic scene", "Integrated capture + API"},
};

} // namespace

QWidget* createFeatureCatalogPage(QTabWidget* applicationTabs) {
    auto* page=new QWidget;
    auto* layout=new QVBoxLayout{page};
    auto* note=new QLabel{
        "Search the RC capability inventory. Interactive entries run within the "
        "indicated tab; API + CTest entries are library functionality without "
        "a dedicated GUI runner. Optional backends require CMake flags and "
        "sometimes external model files. Selecting a row opens its nearest "
        "demo tab; it does not pretend an API-only feature has a GUI."};
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* search=new QLineEdit;
    search->setPlaceholderText("Filter by algorithm, domain, backend or demo location");
    search->setObjectName("capabilitySearch");
    layout->addWidget(search);

    auto* table=new QTableWidget;
    table->setObjectName("capabilityTable");
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({"Group","Capability","Where to look","Mode / limitations"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setRowCount(static_cast<int>(sizeof(kFeatures)/sizeof(kFeatures[0])));
    int catalogRow=0;
    for(const auto& entry:kFeatures) {
        table->setItem(catalogRow,0,new QTableWidgetItem{entry.group});
        table->setItem(catalogRow,1,new QTableWidgetItem{entry.capability});
        table->setItem(catalogRow,2,new QTableWidgetItem{entry.tab});
        table->setItem(catalogRow,3,new QTableWidgetItem{entry.access});
        ++catalogRow;
    }
    layout->addWidget(table);

    QObject::connect(search,&QLineEdit::textChanged,table,[table](const QString& value) {
        const auto query=value.trimmed();
        for(int row=0;row<table->rowCount();++row) {
            bool match=query.isEmpty();
            for(int column=0;column<table->columnCount() && !match;++column)
                match=table->item(row,column)->text().contains(query,Qt::CaseInsensitive);
            table->setRowHidden(row,!match);
        }
    });

    auto* open=new QPushButton{"Open selected demo tab"};
    open->setObjectName("capabilityOpenTab");
    layout->addWidget(open);
    const auto openSelected=[table,applicationTabs] {
        const int row=table->currentRow();
        if (row<0 || table->isRowHidden(row) || !applicationTabs) return;
        const auto title=table->item(row,2)->text();
        for (int index = 0; index < applicationTabs->count(); ++index) {
            if (applicationTabs->tabText(index) == title) {
                applicationTabs->setCurrentIndex(index);
                return;
            }
            // Some destinations live under Settings or Help instead of
            // occupying a top-level application tab.
            auto* nested = qobject_cast<QTabWidget*>(applicationTabs->widget(index));
            if (!nested) continue;
            for (int child = 0; child < nested->count(); ++child) {
                if (nested->tabText(child) == title) {
                    nested->setCurrentIndex(child);
                    applicationTabs->setCurrentIndex(index);
                    return;
                }
            }
        }
    };
    QObject::connect(open,&QPushButton::clicked,page,openSelected);
    QObject::connect(table,&QTableWidget::cellDoubleClicked,page,
                     [openSelected](int,int){openSelected();});
    return page;
}
