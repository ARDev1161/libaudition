#include "temporal_events_page.hpp"

#include <audition/temporal/scalar_analyzer.hpp>
#include <audition/temporal/event_episode_associator.hpp>
#include <audition/pipeline/temporal_acoustic_event_detector.hpp>

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::vector<double> parseValues(const QString& input, bool probabilities) {
    const auto parts = input.split(
        QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts);
    if (parts.size()<2 || parts.size()>64)
        throw std::invalid_argument("Expected 2..64 observations");
    std::vector<double> result;
    result.reserve(static_cast<std::size_t>(parts.size()));
    for (const auto& token : parts) {
        bool valid=false;
        const double value=token.toDouble(&valid);
        if (!valid || !std::isfinite(value) ||
            (probabilities && (value<0.0 || value>1.0)))
            throw std::invalid_argument(
                probabilities ? "Scores must be finite in [0,1]"
                              : "Scalar features must be finite numbers");
        result.push_back(value);
    }
    return result;
}

QString execute(const std::vector<double>& scalars,
                const std::vector<double>& scores,
                double startThreshold, double stopThreshold) {
    using namespace audition;
    using namespace audition::temporal;
    if (startThreshold <= stopThreshold)
        throw std::invalid_argument(
            "Start threshold must be greater than stop threshold");

    TemporalScalarOptions options{};
    options.alpha=1.0; // Display raw scalar changes with no EMA lag.
    options.drift=0.0;
    options.threshold=1.0;
    TemporalScalarAnalyzer scalar{options};
    const auto at=[](std::size_t frame) {
        return Timestamp{static_cast<std::int64_t>(frame)*1000000000LL,
                         {ClockDomain::Monotonic,0U}};
    };
    std::size_t positive=0,negative=0;
    for(std::size_t i=0;i<scalars.size();++i) {
        const auto item=scalar.observe(
            {AcousticSourceId{1},"user-feature",at(i),scalars[i]});
        positive+=item.positive_change ? 1U : 0U;
        negative+=item.negative_change ? 1U : 0U;
    }

    TemporalAcousticEventDetectorOptions detectorOptions{};
    detectorOptions.start_threshold=Probability::from(startThreshold);
    detectorOptions.stop_threshold=Probability::from(stopThreshold);
    TemporalAcousticEventDetector detector{detectorOptions};
    EventEpisodeAssociator episodes;
    std::size_t starts=0,ends=0;
    QStringList transitions;
    for(std::size_t i=0;i<scores.size();++i) {
        ClassificationResult result{};
        result.classes.push_back({"user-label",Probability::from(scores[i])});
        for(const auto& transition : detector.observe(
                AcousticSourceId{1},at(i),result)) {
            const bool active=transition.kind==TemporalAcousticEventKind::Started;
            const auto associated=episodes.observe({
                transition.source_id,transition.label,at(i),active});
            if(associated.size()!=1)
                throw std::runtime_error("Unexpected episode transition");
            starts+=active ? 1U : 0U;
            ends+=active ? 0U : 1U;
            transitions << QStringLiteral("%1: %2 occurrence=%3 episode=%4")
                .arg(static_cast<qulonglong>(i))
                .arg(active ? QStringLiteral("started") : QStringLiteral("ended"))
                .arg(static_cast<qulonglong>(transition.occurrence_id))
                .arg(static_cast<qulonglong>(associated.front().episode_id));
        }
    }
    return QStringLiteral(
        "scalar: positive=%1 negative=%2\n"
        "hysteresis: starts=%3 ends=%4 active=%5\n"
        "transitions:\n%6")
        .arg(static_cast<qulonglong>(positive))
        .arg(static_cast<qulonglong>(negative))
        .arg(static_cast<qulonglong>(starts))
        .arg(static_cast<qulonglong>(ends))
        .arg(static_cast<qulonglong>(detector.activeEvents().size()))
        .arg(transitions.isEmpty() ? QStringLiteral("(none)")
                                   : transitions.join(QLatin1Char('\n')));
}

} // namespace

QWidget* createTemporalEventsPanel(QWidget* parent) {
    auto* group=new QGroupBox{"Scalar changes and acoustic event lifecycle",parent};
    auto* layout=new QVBoxLayout{group};
    auto* help=new QLabel{
        "Edit finite scalar observations and classifier confidence scores. "
        "Scalar values run through source-local CUSUM; classifier scores use "
        "two-observation start/stop hysteresis and persistent-source episode "
        "association. All scores and timestamps are synthetic and supplied "
        "by this example, not inferred from WAV or live microphone input."};
    help->setWordWrap(true);
    layout->addWidget(help);
    auto* form=new QFormLayout;
    auto* scalars=new QPlainTextEdit{"0 0 3 3 3"};
    scalars->setObjectName("temporalScalarInput");
    scalars->setMaximumHeight(55);
    form->addRow("Scalar feature sequence",scalars);
    auto* scores=new QPlainTextEdit{"0.8 0.9 0.1 0.1"};
    scores->setObjectName("temporalScoreInput");
    scores->setMaximumHeight(55);
    form->addRow("Classifier score sequence [0,1]",scores);
    auto* start=new QDoubleSpinBox;
    start->setObjectName("temporalEventStartThreshold");
    start->setRange(0.01,1.0);
    start->setDecimals(2);
    start->setSingleStep(0.05);
    start->setValue(0.70);
    form->addRow("Start threshold",start);
    auto* stop=new QDoubleSpinBox;
    stop->setObjectName("temporalEventStopThreshold");
    stop->setRange(0.0,0.99);
    stop->setDecimals(2);
    stop->setSingleStep(0.05);
    stop->setValue(0.40);
    form->addRow("Stop threshold",stop);
    layout->addLayout(form);
    auto* run=new QPushButton{"Run scalar changes and event episodes"};
    run->setObjectName("temporalEventRun");
    layout->addWidget(run);
    auto* output=new QPlainTextEdit;
    output->setObjectName("temporalEventOutput");
    output->setMinimumHeight(120);
    output->setReadOnly(true);
    layout->addWidget(output);
    QObject::connect(run,&QPushButton::clicked,group,
                     [scalars,scores,start,stop,output] {
        try {
            output->setPlainText(execute(
                parseValues(scalars->toPlainText(),false),
                parseValues(scores->toPlainText(),true),
                start->value(),stop->value()));
        } catch(const std::exception& error) {
            output->setPlainText(
                QStringLiteral("error: ") + QString::fromUtf8(error.what()));
        }
    });
    run->click();
    return group;
}
