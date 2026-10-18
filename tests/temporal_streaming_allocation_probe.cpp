#include <audition/temporal/streaming_pattern_matcher.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>
#include <utility>
#include <vector>

// Standalone executable to keep global allocation instrumentation out of
// GoogleTest and other library tests. Counts C++ new/new[] only, not malloc.
namespace {
std::atomic<std::size_t> allocations{0};
}
void* operator new(std::size_t bytes) {
    if (void* p=std::malloc(bytes?bytes:1)) {
        allocations.fetch_add(1,std::memory_order_relaxed);
        return p;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) {
    if (void* p=std::malloc(bytes?bytes:1)) {
        allocations.fetch_add(1,std::memory_order_relaxed);
        return p;
    }
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}

int main() {
    audition::temporal::StreamingPatternOptions options{};
    options.max_sources=10;
    options.max_templates=8;
    options.max_frames=24;
    options.min_candidate_frames=6;
    options.max_candidate_frames=10;
    audition::temporal::StreamingPatternMatcher matcher{options};
    for (std::size_t i=0;i<8;++i) {
        std::vector<std::vector<double>> reference;
        for (std::size_t k=0;k<8;++k)
            reference.push_back({static_cast<double>(i),static_cast<double>(k)});
        matcher.registerTemplate("pattern_"+std::to_string(i),std::move(reference));
    }
    constexpr std::size_t source_count=10;
    constexpr std::size_t warmup_frames=30;
    constexpr std::size_t measured_frames=100;
    std::size_t matches=0;
    for (std::size_t frame=0;frame<warmup_frames;++frame)
        for (std::size_t source=1;source<=source_count;++source)
            matches+=matcher.push(audition::AcousticSourceId{static_cast<std::uint64_t>(source)},
                                  {static_cast<double>(source%8),static_cast<double>(frame%8)}).size();
    const auto before=allocations.load(std::memory_order_relaxed);
    for (std::size_t frame=warmup_frames;frame<warmup_frames+measured_frames;++frame)
        for (std::size_t source=1;source<=source_count;++source)
            matches+=matcher.push(audition::AcousticSourceId{static_cast<std::uint64_t>(source)},
                                  {static_cast<double>(source%8),static_cast<double>(frame%8)}).size();
    const auto allocated=allocations.load(std::memory_order_relaxed)-before;
    const auto calls=source_count*measured_frames;
    std::cout<<"[temporal-stream-allocations] sources="<<source_count
             <<" templates=8 calls="<<calls
             <<" cpp_new_calls="<<allocated
             <<" new_calls_per_push="<<static_cast<double>(allocated)/static_cast<double>(calls)
             <<" matches="<<matches<<"\n";
    return 0;
}
