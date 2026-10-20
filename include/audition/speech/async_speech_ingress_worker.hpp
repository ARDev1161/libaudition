#pragma once

#include <audition/speech/bounded_speech_pcm_ingress.hpp>
#include <audition/speech/per_source_speech_runtime.hpp>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace audition {

// Explicit application-facing thread boundary. The caller owns both this
// adapter and the longer-lived PerSourceSpeechRuntime. Only its one worker
// calls push/retire/forget on the VAD runtime; the consumer thread applies
// all acknowledgements to its own WAV/timeline coordinator.
class AsyncSpeechIngressWorker final {
public:
    struct Acknowledgement {
        enum class Kind { Bound, Retired, Forgot, Error };
        Kind kind{Kind::Bound};
        std::uint64_t source_id{0U};
        std::uint64_t epoch{0U};
        std::optional<std::uint64_t> generation{};
        std::string error{};
    };

    explicit AsyncSpeechIngressWorker(
        PerSourceSpeechRuntime& runtime,
        SpeechPcmIngressOptions ingress_options = {},
        std::size_t max_acknowledgements = 256U)
        : ingress_(ingress_options), runtime_(runtime),
          max_acknowledgements_(max_acknowledgements) {
        if (!max_acknowledgements_)
            throw std::invalid_argument("Speech acknowledgement capacity must be positive");
        worker_=std::thread{[this] { work(); }};
    }

    ~AsyncSpeechIngressWorker() {
        ingress_.close();
        {
            std::lock_guard<std::mutex> lock{mutex_};
            shutting_down_=true;
        }
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

    AsyncSpeechIngressWorker(const AsyncSpeechIngressWorker&) = delete;
    AsyncSpeechIngressWorker& operator=(const AsyncSpeechIngressWorker&) = delete;

    [[nodiscard]] SpeechPcmAdmission push(std::uint64_t source_id, AudioView audio) {
        return ingress_.push(source_id,audio);
    }
    [[nodiscard]] std::optional<std::uint64_t> sourceEpoch(
        std::uint64_t source_id) const {
        return ingress_.sourceEpoch(source_id);
    }
    [[nodiscard]] std::optional<std::uint64_t> retire(std::uint64_t source_id) {
        // Atomic with respect to forget(), push() and numeric ID reuse.
        return ingress_.retireWithEpoch(source_id);
    }
    void forget(std::uint64_t source_id) { ingress_.forget(source_id); }
    void closeInput() { ingress_.close(); }
    [[nodiscard]] bool finished() const noexcept { return finished_.load(); }
    [[nodiscard]] SpeechPcmIngressStatistics inputStatistics() const {
        return ingress_.statistics();
    }

    [[nodiscard]] std::vector<Acknowledgement> drainAcknowledgements() {
        std::lock_guard<std::mutex> lock{mutex_};
        std::vector<Acknowledgement> result;
        result.reserve(acknowledgements_.size());
        for (auto& item:acknowledgements_) result.push_back(std::move(item));
        acknowledgements_.clear();
        cv_.notify_one();
        return result;
    }

private:
    void publish(Acknowledgement event) {
        std::unique_lock<std::mutex> lock{mutex_};
        cv_.wait(lock,[this] {
            return shutting_down_ ||
                   acknowledgements_.size()<max_acknowledgements_;
        });
        if (shutting_down_) return;
        acknowledgements_.push_back(std::move(event));
    }

    void work() noexcept {
        // A single entry per current numeric source ID avoids sending a
        // Bind notification for every incoming audio chunk.
        std::map<std::uint64_t,std::pair<std::uint64_t,std::uint64_t>> bound;
        while (auto event=ingress_.waitPop()) {
            const auto id=event->source_id;
            const auto epoch=event->epoch;
            try {
                switch(event->kind) {
                case SpeechPcmIngressEvent::Kind::Audio: {
                    runtime_.push(id,event->audio.view());
                    // Per-source VAD failures are reported via trackStatus().
                    const auto status=runtime_.trackStatus(id);
                    if (status.error) {
                        const auto failed_generation=runtime_.activeGeneration(id);
                        runtime_.forget(id);
                        bound.erase(id);
                        ingress_.forget(id);
                        publish({Acknowledgement::Kind::Error,id,epoch,
                                 failed_generation,*status.error});
                        break;
                    }
                    const auto generation=runtime_.activeGeneration(id);
                    if (generation) {
                        const auto last=bound.find(id);
                        if (last==bound.end() ||
                            last->second.first!=epoch ||
                            last->second.second!=*generation) {
                            bound[id]={epoch,*generation};
                            publish({Acknowledgement::Kind::Bound,id,epoch,
                                     generation,{}});
                        }
                    }
                    break;
                }
                case SpeechPcmIngressEvent::Kind::Forget: {
                    const auto before=runtime_.activeGeneration(id);
                    runtime_.forget(id);
                    bound.erase(id);
                    publish({Acknowledgement::Kind::Forgot,id,epoch,before,{}});
                    break;
                }
                case SpeechPcmIngressEvent::Kind::Retire: {
                    auto generation=runtime_.retire(id);
                    if (!generation && runtime_.activeGeneration(id)) {
                        // The ingress already retired this epoch, but the
                        // runtime cannot flush while retirement slots are full.
                        runtime_.forget(id);
                        bound.erase(id);
                        publish({Acknowledgement::Kind::Error,id,epoch,
                                 std::nullopt,
                                 "Speech retirement capacity exhausted; final utterance discarded"});
                    }
                    bound.erase(id);
                    publish({Acknowledgement::Kind::Retired,id,epoch,
                             generation,{}});
                    break;
                }
                }
            } catch (const std::exception& error) {
                try { runtime_.forget(id); } catch (...) {}
                // Failed Audio processing invalidates all queued same-source PCM.
                if (event->kind==SpeechPcmIngressEvent::Kind::Audio)
                    ingress_.forget(id);
                bound.erase(id);
                publish({Acknowledgement::Kind::Error,id,epoch,std::nullopt,
                         error.what()});
                if (event->kind==SpeechPcmIngressEvent::Kind::Retire)
                    publish({Acknowledgement::Kind::Retired,id,epoch,
                             std::nullopt,{}});
            } catch (...) {
                try { runtime_.forget(id); } catch (...) {}
                // Failed Audio processing invalidates all queued same-source PCM.
                if (event->kind==SpeechPcmIngressEvent::Kind::Audio)
                    ingress_.forget(id);
                bound.erase(id);
                publish({Acknowledgement::Kind::Error,id,epoch,
                         std::nullopt,"Unknown asynchronous speech error"});
                if (event->kind==SpeechPcmIngressEvent::Kind::Retire)
                    publish({Acknowledgement::Kind::Retired,id,epoch,
                             std::nullopt,{}});
            }
        }
        finished_.store(true);
    }

    BoundedSpeechPcmIngress ingress_;
    PerSourceSpeechRuntime& runtime_;
    const std::size_t max_acknowledgements_;
    mutable std::mutex mutex_{};
    std::condition_variable cv_{};
    std::deque<Acknowledgement> acknowledgements_{};
    bool shutting_down_{false};
    std::atomic<bool> finished_{false};
    std::thread worker_{};
};

} // namespace audition
