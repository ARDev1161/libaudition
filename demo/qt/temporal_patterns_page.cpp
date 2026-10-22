#include "temporal_patterns_page.hpp"

#include <audition/audition.hpp>

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

QStringList tokens(const QString& input) {
    return input.split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                       Qt::SkipEmptyParts);
}

std::vector<std::vector<double>> featureFrames(const QString& input) {
    const auto parts=tokens(input);
    if (parts.isEmpty() || parts.size()>64)
        throw std::invalid_argument("Provide 1..64 scalar feature frames");
    std::vector<std::vector<double>> frames;
    frames.reserve(static_cast<std::size_t>(parts.size()));
    for (const auto& part : parts) {
        bool ok=false;
        const double value=part.toDouble(&ok);
        if (!ok || !std::isfinite(value))
            throw std::invalid_argument("Feature frames must be finite numbers");
        frames.push_back({value});
    }
    return frames;
}

std::vector<std::int64_t> onsetMilliseconds(const QString& input) {
    const auto parts=tokens(input);
    if (parts.isEmpty() || parts.size()>32)
        throw std::invalid_argument("Provide 1..32 onset times in milliseconds");
    std::vector<std::int64_t> onsets;
    for (const auto& part : parts) {
        bool ok=false;
        const auto value=part.toLongLong(&ok);
        if (!ok || value<0 || value>1000000000LL ||
            (!onsets.empty() && value<=onsets.back()))
            throw std::invalid_argument("Onsets must increase (0..1e9 ms)");
        onsets.push_back(value);
    }
    return onsets;
}

QString report(const std::vector<std::vector<double>>& frames,
               const std::vector<std::int64_t>& onsets,
               std::size_t window,
               double max_cost) {
    using namespace audition;
    using namespace audition::temporal;

    RecurrenceAnalyzer recurrence;
    RecurrenceEstimate recurrence_result{};
    for (auto milliseconds : onsets) {
        recurrence_result=recurrence.observe(
            AcousticSourceId{1}, "pulse",
            Timestamp{milliseconds*1000000LL,
                      {ClockDomain::Monotonic, 0U}});
    }

    QStringList results;
    results << QStringLiteral("recurrence: periodic=%1 intervals=%2 median_period_ms=%3 jitter_ms=%4")
        .arg(recurrence_result.periodic ? "yes" : "no")
        .arg(static_cast<qulonglong>(recurrence_result.intervals))
        .arg(recurrence_result.median_period.nanoseconds()/1000000LL)
        .arg(recurrence_result.median_absolute_deviation.nanoseconds()/1000000LL);

    if (frames.size()<window*2) {
        results << QStringLiteral("dtw: requires at least two complete windows");
        results << QStringLiteral("motif: requires at least two complete windows");
    } else {
        const auto size=static_cast<std::ptrdiff_t>(window);
        std::vector<std::vector<double>> first(frames.begin(), frames.begin()+size);
        std::vector<std::vector<double>> second(frames.begin()+size,
                                                frames.begin()+2*size);
        DynamicTimeWarping dtw;
        const auto aligned=dtw.compare(first,second);
        results << QStringLiteral("dtw: reachable=%1 normalized_cost=%2 path_frames=%3")
            .arg(aligned.reachable ? "yes" : "no")
            .arg(aligned.normalized_cost,0,'g',8)
            .arg(static_cast<qulonglong>(aligned.path_length));
        MotifOptions options{};
        options.window_frames=window;
        options.max_normalized_cost=max_cost;
        const auto motif=AcousticMotifFinder{options}.find(frames);
        results << QStringLiteral("motif: %1 first=%2 second=%3 cost=%4")
            .arg(motif.found ? "found" : "none")
            .arg(static_cast<qulonglong>(motif.first_start))
            .arg(static_cast<qulonglong>(motif.second_start))
            .arg(motif.normalized_cost,0,'g',8);
    }

    StreamingPatternOptions stream_options{};
    stream_options.max_normalized_cost=max_cost;
    stream_options.cooldown_frames=window>0 ? window-1 : 0;
    StreamingPatternMatcher matcher{stream_options};
    std::vector<std::vector<double>> reference;
    reference.assign(frames.begin(),
                     frames.begin()+static_cast<std::ptrdiff_t>(
                         std::min(window,frames.size())));
    matcher.registerTemplate("pulse-shape",reference);
    std::size_t hits=0;
    for (const auto& frame : frames)
        hits+=matcher.push(AcousticSourceId{1},frame).size();
    results << QStringLiteral("stream: matches=%1 (template = first %2 frames)")
        .arg(static_cast<qulonglong>(hits))
        .arg(static_cast<qulonglong>(reference.size()));
    return results.join(QLatin1Char('\n'));
}

}  // namespace

QWidget* createTemporalPatternsPage() {
    auto* page=new QWidget;
    auto* layout=new QVBoxLayout{page};
    auto* description=new QLabel{
        "Explore four real backend-neutral temporal algorithms on scalar feature "
        "frames and event onsets. Data are illustrative, not model predictions "
        "or measured acoustic accuracy."};
    description->setWordWrap(true);
    layout->addWidget(description);

    auto* form=new QFormLayout;
    auto* featureInput=new QLineEdit{"1 2 3 1 2 3 1 2 3"};
    featureInput->setObjectName("temporalFeatureFrames");
    featureInput->setToolTip("Finite numeric feature values, separated by spaces or commas");
    form->addRow("Scalar feature frames",featureInput);
    auto* onsetInput=new QLineEdit{"0 100 200 300"};
    onsetInput->setObjectName("temporalOnsetTimes");
    form->addRow("Event onsets (ms)",onsetInput);
    auto* window=new QSpinBox;
    window->setObjectName("temporalWindowFrames");
    window->setRange(1,32);
    window->setValue(3);
    form->addRow("Pattern window (frames)",window);
    auto* tolerance=new QDoubleSpinBox;
    tolerance->setObjectName("temporalPatternMaxCost");
    tolerance->setRange(0,1000);
    tolerance->setDecimals(5);
    tolerance->setSingleStep(0.01);
    tolerance->setValue(0.001);
    form->addRow("Max normalized DTW cost",tolerance);
    layout->addLayout(form);

    auto* run=new QPushButton{"Run temporal examples"};
    run->setObjectName("temporalRun");
    layout->addWidget(run);
    auto* output=new QPlainTextEdit;
    output->setObjectName("temporalDemoOutput");
    output->setReadOnly(true);
    output->setMinimumHeight(170);
    layout->addWidget(output);
    QObject::connect(run,&QPushButton::clicked,page,
                     [featureInput,onsetInput,window,tolerance,output] {
        try {
            output->setPlainText(report(
                featureFrames(featureInput->text()),
                onsetMilliseconds(onsetInput->text()),
                static_cast<std::size_t>(window->value()),
                tolerance->value()));
        } catch (const std::exception& error) {
            output->setPlainText(
                QStringLiteral("error: ") + QString::fromUtf8(error.what()));
        }
    });
    run->click();
    return page;
}
