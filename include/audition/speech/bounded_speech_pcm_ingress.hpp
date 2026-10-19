#pragma once

#include <audition/audio/audio_buffer.hpp>
#include <audition/core/export.hpp>
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace audition {

// Bounded capture-to-VAD transport, with no model inference or internally
// spawned threads. Applications own the consumer thread and dispatch events
// serially to PerSourceSpeechRuntime. This is transport, not a second VAD/ASR.
struct SpeechPcmIngressOptions {
    std::uint32_t sample_rate_hz{16000U};
    std::size_t max_sources{16U};
    std::size_t max_pending_chunks{256U};
    std::size_t max_pending_samples{16000U * 16U};
    std::size_t max_chunk_samples{16000U};
};

enum class SpeechPcmAdmission {
    Accepted, RejectedCapacity, RejectedSourceLimit, Closed
};

struct SpeechPcmIngressEvent {
    enum class Kind { Audio, Forget };
    Kind kind{Kind::Audio};
    std::uint64_t source_id{0U};
    std::uint64_t epoch{0U};
    AudioBuffer audio{};
};

struct SpeechPcmIngressStatistics {
    std::size_t sources{0U};
    std::size_t pending_chunks{0U};
    std::size_t pending_samples{0U};
    std::size_t pending_invalidations{0U};
    std::uint64_t accepted_chunks{0U};
    std::uint64_t rejected_chunks{0U};
    std::uint64_t invalidated_chunks{0U};
};

// push() copies at most max_chunk_samples and never performs VAD/ASR work.
// Overflow for an existing source is a PCM discontinuity: all of its queued
// chunks are removed, and a coalesced Forget event is prioritized ahead of
// future Audio events. A consumer MUST dispatch each Forget to
// PerSourceSpeechRuntime::forget(source_id) before consuming subsequent PCM.
class AUDITION_API BoundedSpeechPcmIngress final {
public:
    explicit BoundedSpeechPcmIngress(SpeechPcmIngressOptions options = {})
        : options_(options) {
        if (!options_.sample_rate_hz || !options_.max_sources ||
            !options_.max_pending_chunks || !options_.max_pending_samples ||
            !options_.max_chunk_samples ||
            options_.max_chunk_samples > options_.sample_rate_hz ||
            options_.max_chunk_samples > options_.max_pending_samples)
            throw std::invalid_argument("Invalid bounded speech PCM ingress options");
    }

    BoundedSpeechPcmIngress(const BoundedSpeechPcmIngress&) = delete;
    BoundedSpeechPcmIngress& operator=(const BoundedSpeechPcmIngress&) = delete;

    SpeechPcmAdmission push(std::uint64_t source_id, AudioView audio) {
        if (audio.format().sample_rate_hz != options_.sample_rate_hz ||
            audio.format().channel_count != 1U ||
            audio.format().layout != AudioLayout::Interleaved ||
            audio.sampleCount() > options_.max_chunk_samples ||
            (audio.sampleCount() && !audio.data()))
            throw std::invalid_argument("Speech PCM ingress expects bounded interleaved mono audio");
        for (std::size_t i=0;i<audio.sampleCount();++i)
            if (!std::isfinite(audio.data()[i]))
                throw std::invalid_argument("Nonfinite speech PCM ingress sample");
        if (!audio.sampleCount()) return SpeechPcmAdmission::Accepted;
        // Allocate/copy outside the queue lock; never call a model here.
        std::vector<float> copy(audio.data(),audio.data()+audio.sampleCount());
        std::lock_guard<std::mutex> lock{mutex_};
        if (closed_) return SpeechPcmAdmission::Closed;
        auto existing=sources_.find(source_id);
        if (existing==sources_.end() && sources_.size()>=options_.max_sources) {
            ++rejected_;
            return SpeechPcmAdmission::RejectedSourceLimit;
        }
        if (pending_.size()>=options_.max_pending_chunks ||
            copy.size()>options_.max_pending_samples-pending_samples_) {
            ++rejected_;
            // Existing source loses continuity. Also discard its buffered
            // audio to prevent a partial phrase spanning the dropped call.
            if (existing!=sources_.end()) invalidateLocked(source_id);
            return SpeechPcmAdmission::RejectedCapacity;
        }
        if (existing==sources_.end()) {
            const auto epoch=next_epoch_++;
            existing=sources_.emplace(source_id,epoch).first;
        }
        SpeechPcmIngressEvent entry{};
        entry.kind=SpeechPcmIngressEvent::Kind::Audio;
        entry.source_id=source_id;
        entry.epoch=existing->second;
        entry.audio=AudioBuffer{std::move(copy),audio.format(),
                                audio.captureTime(),audio.sequenceNumber()};
        pending_samples_+=entry.audio.samples().size();
        pending_.push_back(std::move(entry));
        ++accepted_;
        cv_.notify_one();
        return SpeechPcmAdmission::Accepted;
    }

    // Permanently ends the current producer epoch. The consumer receives a
    // Forget event before any subsequent audio for the reused numeric ID.
    void forget(std::uint64_t source_id) {
        std::lock_guard<std::mutex> lock{mutex_};
        if (sources_.erase(source_id)) {
            discardQueuedLocked(source_id);
            invalidations_[source_id]=next_epoch_++;
            cv_.notify_one();
        }
    }

    [[nodiscard]] std::optional<SpeechPcmIngressEvent> tryPop() {
        std::lock_guard<std::mutex> lock{mutex_};
        return popLocked();
    }

    [[nodiscard]] std::optional<SpeechPcmIngressEvent> waitPop() {
        std::unique_lock<std::mutex> lock{mutex_};
        cv_.wait(lock,[this] {
            return closed_ || !invalidations_.empty() || !pending_.empty();
        });
        return popLocked();
    }

    void close() {
        std::lock_guard<std::mutex> lock{mutex_};
        closed_=true;
        cv_.notify_all();
    }

    [[nodiscard]] SpeechPcmIngressStatistics statistics() const {
        std::lock_guard<std::mutex> lock{mutex_};
        return {sources_.size(),pending_.size(),pending_samples_,
                invalidations_.size(),accepted_,rejected_,invalidated_};
    }

private:
    void discardQueuedLocked(std::uint64_t id) {
        for (auto it=pending_.begin();it!=pending_.end();) {
            if (it->source_id==id) {
                pending_samples_-=it->audio.samples().size();
                ++invalidated_;
                it=pending_.erase(it);
            } else ++it;
        }
    }
    void invalidateLocked(std::uint64_t id) {
        discardQueuedLocked(id);
        sources_[id]=next_epoch_++;
        invalidations_[id]=sources_[id];
        cv_.notify_one();
    }
    std::optional<SpeechPcmIngressEvent> popLocked() {
        if (!invalidations_.empty()) {
            const auto item=*invalidations_.begin();
            invalidations_.erase(invalidations_.begin());
            SpeechPcmIngressEvent result{};
            result.kind=SpeechPcmIngressEvent::Kind::Forget;
            result.source_id=item.first;
            result.epoch=item.second;
            return result;
        }
        if (pending_.empty()) return std::nullopt;
        auto result=std::move(pending_.front());
        pending_.pop_front();
        pending_samples_-=result.audio.samples().size();
        return result;
    }

    SpeechPcmIngressOptions options_{};
    mutable std::mutex mutex_{};
    std::condition_variable cv_{};
    std::map<std::uint64_t,std::uint64_t> sources_{};
    std::map<std::uint64_t,std::uint64_t> invalidations_{};
    std::deque<SpeechPcmIngressEvent> pending_{};
    std::uint64_t next_epoch_{1U};
    std::size_t pending_samples_{0U};
    std::uint64_t accepted_{0U},rejected_{0U},invalidated_{0U};
    bool closed_{false};
};

} // namespace audition
