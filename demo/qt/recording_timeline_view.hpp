#pragma once

#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonObject>
#include <QMouseEvent>
#include <QScrollBar>
#include <QString>
#include <QVariant>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace demo {

// Visual-only Qt adapter. All event positions come from libaudition's
// recording-relative sample-index timeline; no inference in the UI thread.
class RecordingTimelineView final : public QGraphicsView {
public:
    explicit RecordingTimelineView(QWidget* parent = nullptr)
        : QGraphicsView(parent), scene_(new QGraphicsScene(this)) {
        setScene(scene_);
        setMinimumHeight(95);
        setMaximumHeight(120);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setRenderHint(QPainter::Antialiasing);
    }

    void setSeekHandler(std::function<void(qint64)> handler) {
        seek_ = std::move(handler);
    }

    void load(const QJsonArray& events, std::uint32_t rate, bool sound,
              qint64 durationMs) {
        scene_->clear();
        cursor_ = nullptr;
        rate_ = rate;
        const auto width = std::max(600.0, static_cast<double>(durationMs) * pixelsPerMs_);
        scene_->setSceneRect(0, 0, width, 72);
        for (const auto& item : events) {
            if (!item.isObject() || !rate) continue;
            const auto object = item.toObject();
            const auto start = object.value("start_sample").toDouble(-1);
            const auto end = object.value("end_sample").toDouble(-1);
            if (!std::isfinite(start) || !std::isfinite(end) ||
                start < 0 || end < start) continue;
            const auto x = start * 1000.0 / static_cast<double>(rate) * pixelsPerMs_;
            const auto right = end * 1000.0 / static_cast<double>(rate) * pixelsPerMs_;
            const auto label = object.value(sound ? "label" : "text").toString();
            if (label.isEmpty()) continue;
            const auto span = std::max(12.0, right - x);
            const auto color = sound ? QColor(103, 155, 115, 100) : QColor(88, 137, 197, 100);
            auto* rect = scene_->addRect(x, 12, span, 35, QPen(Qt::NoPen), QBrush(color));
            rect->setData(0, x);
            auto* textItem = scene_->addText(label);
            textItem->setPos(x + 2, 15);
            textItem->setTextWidth(std::min(400.0, std::max(span, 80.0)));
            textItem->setData(0, x);
        }
        cursor_ = scene_->addLine(0, 0, 0, 65, QPen(QColor(220, 75, 75), 2));
        cursor_->setZValue(10);
        setPosition(0);
    }

    void setPosition(qint64 ms) {
        const double x = static_cast<double>(std::max<qint64>(0, ms)) * pixelsPerMs_;
        if (cursor_) cursor_->setLine(x, 0, x, 65);
        // Keep playback cursor in the same viewport location for both lanes.
        horizontalScrollBar()->setValue(
            static_cast<int>(std::max(0.0, x - viewport()->width() * 0.45)));
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && seek_) {
            const auto x = mapToScene(event->pos()).x();
            const auto ms = std::max(0.0, x / pixelsPerMs_);
            if (ms < static_cast<double>(std::numeric_limits<qint64>::max()))
                seek_(static_cast<qint64>(ms));
        }
        QGraphicsView::mousePressEvent(event);
    }

private:
    static constexpr double pixelsPerMs_ = 0.10; // 100 px / second
    QGraphicsScene* scene_;
    QGraphicsLineItem* cursor_{nullptr};
    std::uint32_t rate_{0};
    std::function<void(qint64)> seek_{};
};
} // namespace demo
