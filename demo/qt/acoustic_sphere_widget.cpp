#include "acoustic_sphere_widget.hpp"

#include <QColor>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QRadialGradient>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr int kTrailLength = 100;
QVector3D pointOnSphere(double latitude, double longitude) {
    return {
        static_cast<float>(std::cos(latitude) * std::cos(longitude)),
        static_cast<float>(std::cos(latitude) * std::sin(longitude)),
        static_cast<float>(std::sin(latitude))};
}
double degrees(double radians) { return radians * 180.0 / kPi; }
}

AcousticSphereWidget::AcousticSphereWidget(QWidget* parent)
    : QWidget{parent} {
    setObjectName("acousticSphere");
    setMinimumSize(340, 360);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setToolTip("Drag to orbit · Mouse wheel to zoom · Double-click to reset");
}

void AcousticSphereWidget::setTracks(const std::vector<AcousticSceneTrack>& tracks) {
    tracks_.clear();
    for (const auto& track : tracks) {
        if (!std::isfinite(track.direction.x()) ||
            !std::isfinite(track.direction.y()) ||
            !std::isfinite(track.direction.z()) ||
            track.direction.lengthSquared() < 1.0e-12F) {
            continue;
        }
        auto valid = track;
        valid.direction.normalize();
        valid.activity = std::clamp(valid.activity, 0.0, 1.0);
        tracks_.push_back(valid);
        auto& trail = trails_[valid.id];
        if (trail.empty() || (trail.back() - valid.direction).lengthSquared() > 1e-6F) {
            trail.push_back(valid.direction);
            if (trail.size() > kTrailLength) {
                trail.pop_front();
            }
        }
    }
    update();
}

void AcousticSphereWidget::setPotentials(
    const std::vector<AcousticScenePotential>& potentials) {
    potentials_.clear();
    for (auto potential : potentials) {
        if (!std::isfinite(potential.direction.x()) ||
            !std::isfinite(potential.direction.y()) ||
            !std::isfinite(potential.direction.z()) ||
            !std::isfinite(potential.score) ||
            potential.score <= 0.0 ||
            potential.direction.lengthSquared() < 1.0e-12F) {
            continue;
        }
        potential.direction.normalize();
        potentials_.push_back(potential);
    }
    update();
}

void AcousticSphereWidget::clearTracks() {
    tracks_.clear();
    potentials_.clear();
    trails_.clear();
    selected_ = 0;
    hovered_ = 0;
    update();
}

void AcousticSphereWidget::setTrackClicked(
    std::function<void(std::uint64_t)> callback) {
    onTrackClicked_ = std::move(callback);
}

std::size_t AcousticSphereWidget::trackCount() const noexcept {
    return tracks_.size();
}

std::uint64_t AcousticSphereWidget::selectedTrack() const noexcept {
    return selected_;
}

AcousticSphereWidget::Projected AcousticSphereWidget::project(
    const QVector3D& point) const {
    const auto forward = QVector3D{
        static_cast<float>(std::cos(pitch_) * std::cos(yaw_)),
        static_cast<float>(std::cos(pitch_) * std::sin(yaw_)),
        static_cast<float>(std::sin(pitch_))};
    const auto right = QVector3D::crossProduct(
        QVector3D{0, 0, 1}, forward).normalized();
    const auto up = QVector3D::crossProduct(forward, right).normalized();
    const double scale = std::min(width() * 0.39, height() * 0.39) * zoom_;
    const QPointF center(width() * 0.5, height() * 0.50);
    return {
        center + QPointF(QVector3D::dotProduct(point, right) * scale,
                         -QVector3D::dotProduct(point, up) * scale),
        QVector3D::dotProduct(point, forward)};
}

QColor AcousticSphereWidget::trackColor(std::uint64_t id) const {
    return QColor::fromHsv(
        static_cast<int>((id * 71U + 175U) % 360U), 194, 222);
}

void AcousticSphereWidget::drawGrid(QPainter& painter) const {
    const double radius = std::min(width() * 0.39, height() * 0.39) * zoom_;
    const auto center = project({0, 0, 0}).pixel;
    QRadialGradient fill(center - QPointF(radius * 0.25, radius * 0.25),
                         radius * 1.9);
    fill.setColorAt(0.0, QColor(55, 116, 157, 40));
    fill.setColorAt(1.0, QColor(55, 116, 157, 3));
    painter.setBrush(fill);
    painter.setPen(QPen(QColor(115, 148, 169, 128), 1.4));
    painter.drawEllipse(center, radius, radius);

    // True 3D latitude and longitude rings. Segments behind the sphere
    // are dimmed so the upper/lower elevations remain comprehensible.
    painter.setBrush(Qt::NoBrush);
    const auto drawRing = [&](const auto& makePoint, QColor front, QColor back) {
        constexpr int samples = 144;
        for (int i = 0; i < samples; ++i) {
            const auto a = project(makePoint(i * 2.0 * kPi / samples));
            const auto b = project(makePoint((i + 1) * 2.0 * kPi / samples));
            const bool inFront = (a.depth + b.depth) >= 0;
            painter.setPen(QPen(inFront ? front : back,
                                inFront ? 1.15 : 0.75,
                                inFront ? Qt::SolidLine : Qt::DotLine));
            painter.drawLine(a.pixel, b.pixel);
        }
    };
    for (int elevation = -60; elevation <= 60; elevation += 30) {
        const auto front = elevation < 0
            ? QColor(186, 130, 71, 140) : QColor(62, 149, 202, 158);
        const auto back = elevation < 0
            ? QColor(186, 130, 71, 46) : QColor(62, 149, 202, 46);
        const double lat = elevation * kPi / 180.0;
        drawRing([lat](double lon) { return pointOnSphere(lat, lon); },
                 front, back);
    }
    for (int az = 0; az < 360; az += 30) {
        const double lon = az * kPi / 180.0;
        drawRing([lon](double t) {
            return pointOnSphere(t, lon);
        }, QColor(127, 157, 180, 105), QColor(127, 157, 180, 28));
    }
    const auto equator = project({0, 0, 0}).pixel;
    painter.setPen(QColor(140, 158, 169));
    painter.drawText(QRectF(6, height() - 43, width() - 12, 18),
                     Qt::AlignCenter,
                     "Unit sphere · elevation +90° above / −90° below equator");
    painter.setPen(QPen(QColor(155, 161, 170, 140), 1));
    painter.drawEllipse(equator, 3.0, 3.0);
}

void AcousticSphereWidget::drawAxes(QPainter& painter) const {
    const struct {
        QVector3D dir;
        const char* name;
        QColor color;
    } axes[] = {
        {{1, 0, 0}, "+X forward", QColor(226, 96, 96)},
        {{0, 1, 0}, "+Y left", QColor(77, 174, 115)},
        {{0, 0, 1}, "+Z up", QColor(89, 139, 237)},
    };
    const auto center = project({0, 0, 0}).pixel;
    for (const auto& axis : axes) {
        const auto end = project(axis.dir * 1.2F).pixel;
        painter.setPen(QPen(axis.color, 2));
        painter.drawLine(center, end);
        painter.setPen(axis.color);
        painter.drawText(end + QPointF(5.0, -5.0), axis.name);
    }
    painter.setPen(QColor(140, 158, 169));
    painter.drawText(QRectF(8, 8, width() - 16, 22), Qt::AlignLeft,
                     "Orbit: drag · Zoom: scroll · Reset view: double-click");
}

void AcousticSphereWidget::drawTracks(QPainter& painter) const {
    for (const auto& entry : trails_) {
        const auto color = trackColor(entry.first);
        const auto& trail = entry.second;
        for (std::size_t i = 1; i < trail.size(); ++i) {
            const auto a = project(trail[i - 1]);
            const auto b = project(trail[i]);
            QColor faded = color;
            const double fraction = static_cast<double>(i) / static_cast<double>(trail.size());
            faded.setAlpha(static_cast<int>((a.depth + b.depth < 0 ? 50 : 170) * fraction));
            painter.setPen(QPen(faded, 2.0));
            painter.drawLine(a.pixel, b.pixel);
        }
    }

    // SSL proposals have no persistent ID: small outlined diamonds, not
    // clickable track markers. These are instantaneous direction candidates.
    for (const auto& proposal : potentials_) {
        const auto dot = project(proposal.direction);
        const QColor ink = dot.depth < 0
            ? QColor(105, 146, 166, 90)
            : QColor(104, 182, 225, 190);
        painter.setPen(QPen(ink, 1.4));
        painter.setBrush(Qt::NoBrush);
        const QPointF vertices[4]{
            dot.pixel + QPointF(0.0, -5.0),
            dot.pixel + QPointF(5.0, 0.0),
            dot.pixel + QPointF(0.0, 5.0),
            dot.pixel + QPointF(-5.0, 0.0)};
        painter.drawPolygon(vertices, 4);
    }

    // Render hidden-side sources first and foreground sources last.
    auto sorted = tracks_;
    std::sort(sorted.begin(), sorted.end(), [this](const auto& a, const auto& b) {
        return project(a.direction).depth < project(b.direction).depth;
    });
    for (const auto& track : sorted) {
        const auto dot = project(track.direction);
        const bool back = dot.depth < 0;
        const bool highlight = track.id == hovered_ || track.id == selected_;
        const auto color = trackColor(track.id);
        QColor halo = color;
        halo.setAlpha(back ? 35 : 62);
        painter.setPen(Qt::NoPen);
        painter.setBrush(halo);
        painter.drawEllipse(dot.pixel, highlight ? 17 : 13, highlight ? 17 : 13);
        auto centerColor = color;
        centerColor.setAlpha(back ? 90 : 255);
        painter.setBrush(centerColor);
        painter.setPen(QPen(back ? QColor(160, 170, 183) : QColor(255, 255, 255),
                            highlight ? 2.5 : 1.4));
        painter.drawEllipse(dot.pixel, highlight ? 9 : 7, highlight ? 9 : 7);
        painter.setPen(back ? QColor(160, 170, 183) : color);
        painter.drawText(dot.pixel + QPointF(11, -9),
                         QString("#%1").arg(static_cast<qulonglong>(track.id)));
    }
}

void AcousticSphereWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().base());
    drawGrid(painter);
    drawAxes(painter);
    drawTracks(painter);
}

std::uint64_t AcousticSphereWidget::hitTest(const QPoint& pos) const {
    double best = 17.0 * 17.0;
    std::uint64_t found = 0;
    for (const auto& track : tracks_) {
        const auto p = project(track.direction).pixel;
        const auto dx = p.x() - pos.x();
        const auto dy = p.y() - pos.y();
        const auto distance = dx * dx + dy * dy;
        if (distance < best) {
            best = distance;
            found = track.id;
        }
    }
    return found;
}

void AcousticSphereWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        const auto hit = hitTest(event->pos());
        if (hit != 0) {
            selected_ = hit;
            if (onTrackClicked_) {
                onTrackClicked_(hit);
            }
            update();
            return;
        }
        dragging_ = true;
        lastMouse_ = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void AcousticSphereWidget::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_) {
        const auto delta = event->pos() - lastMouse_;
        lastMouse_ = event->pos();
        yaw_ -= delta.x() * 0.009;
        pitch_ = std::clamp(pitch_ + delta.y() * 0.009, -1.48, 1.48);
        update();
        return;
    }
    const auto id = hitTest(event->pos());
    if (id != hovered_) {
        hovered_ = id;
        update();
        if (id == 0) {
            QToolTip::hideText();
            return;
        }
        for (const auto& track : tracks_) {
            if (track.id != id) {
                continue;
            }
            const auto& v = track.direction;
            const auto azimuth = degrees(std::atan2(v.y(), v.x()));
            const auto elevation = degrees(std::atan2(
                v.z(), std::hypot(v.x(), v.y())));
            const auto classification = track.classification_label.empty()
                ? QString("Not configured / pending")
                : QString::fromStdString(track.classification_label) +
                  QString(" (model probability %1)").arg(
                      track.classification_probability, 0, 'f', 2);
            QToolTip::showText(
                event->globalPos(),
                QString("Track #%1\nAzimuth: %2°\nElevation: %3°\nActivity: %4\nClassification: %5")
                    .arg(static_cast<qulonglong>(track.id))
                    .arg(azimuth, 0, 'f', 1)
                    .arg(elevation, 0, 'f', 1)
                    .arg(track.activity, 0, 'f', 2)
                    .arg(classification),
                this);
            break;
        }
    }
}

void AcousticSphereWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragging_ = false;
        unsetCursor();
    }
}

void AcousticSphereWidget::mouseDoubleClickEvent(QMouseEvent*) {
    yaw_ = -0.8;
    pitch_ = 0.42;
    zoom_ = 1.0;
    update();
}

void AcousticSphereWidget::wheelEvent(QWheelEvent* event) {
    zoom_ = std::clamp(
        zoom_ * (event->angleDelta().y() > 0 ? 1.1 : 1.0 / 1.1),
        0.55, 1.5);
    update();
}
