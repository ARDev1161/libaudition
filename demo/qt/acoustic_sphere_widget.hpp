#pragma once

#include <QPoint>
#include <QPointF>
#include <QVector3D>
#include <QWidget>

#include <cstddef>

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

class QColor;
class QPainter;

// Unit directions in libaudition's sensor frame: +X forward, +Y left, +Z up.
// ODAS provides directions, not metric positions.
struct AcousticSceneTrack {
    std::uint64_t id{0};
    QVector3D direction{};
    double activity{0.0};
    std::string classification_label{};
    double classification_probability{-1.0};
};

struct AcousticScenePotential {
    QVector3D direction{};
    double score{0.0}; // Raw ODAS SSL score (uncalibrated)
};

class AcousticSphereWidget final : public QWidget {
public:
    explicit AcousticSphereWidget(QWidget* parent = nullptr);

    void setTracks(const std::vector<AcousticSceneTrack>& tracks);
    void setPotentials(const std::vector<AcousticScenePotential>& potentials);
    void clearTracks();
    void setTrackClicked(std::function<void(std::uint64_t)> callback);
    [[nodiscard]] std::size_t trackCount() const noexcept;
    [[nodiscard]] std::uint64_t selectedTrack() const noexcept;

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    struct Projected {
        QPointF pixel{};
        double depth{0.0};
    };

    [[nodiscard]] Projected project(const QVector3D& direction) const;
    [[nodiscard]] std::uint64_t hitTest(const QPoint& position) const;
    [[nodiscard]] QColor trackColor(std::uint64_t id) const;
    void drawGrid(QPainter& painter) const;
    void drawAxes(QPainter& painter) const;
    void drawTracks(QPainter& painter) const;

    std::vector<AcousticSceneTrack> tracks_{};
    std::vector<AcousticScenePotential> potentials_{};
    std::map<std::uint64_t, std::deque<QVector3D>> trails_{};
    std::function<void(std::uint64_t)> onTrackClicked_{};
    QPoint lastMouse_{};
    double yaw_{-0.8};
    double pitch_{0.42};
    double zoom_{1.0};
    std::uint64_t selected_{0};
    std::uint64_t hovered_{0};
    bool dragging_{false};
};
