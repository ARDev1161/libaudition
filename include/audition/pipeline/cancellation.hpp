#pragma once

#include <atomic>
#include <memory>

namespace audition {

class CancellationToken {
public:
    CancellationToken() = default;
    [[nodiscard]] bool isCancellationRequested() const noexcept {
        return state_ && state_->load(std::memory_order_acquire);
    }

private:
    friend class CancellationSource;
    explicit CancellationToken(std::shared_ptr<std::atomic_bool> state) : state_(std::move(state)) {}
    std::shared_ptr<std::atomic_bool> state_{};
};

class CancellationSource {
public:
    CancellationSource() : state_(std::make_shared<std::atomic_bool>(false)) {}

    [[nodiscard]] CancellationToken token() const { return CancellationToken{state_}; }
    void cancel() noexcept { state_->store(true, std::memory_order_release); }
    [[nodiscard]] bool isCancellationRequested() const noexcept {
        return state_->load(std::memory_order_acquire);
    }

private:
    std::shared_ptr<std::atomic_bool> state_{};
};

}  // namespace audition
