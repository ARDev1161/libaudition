#include <audition/temporal/streaming_pattern_matcher.hpp>

#include <algorithm>
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
    audition::temporal::StreamingPatternMatcher reusable{options};
    std::vector<audition::temporal::StreamingPatternMatch> reusable_output;
    reusable_output.reserve(options.max_templates);
    for (std::size_t i=0;i<8;++i) {
        std::vector<std::vector<double>> reference;
        for (std::size_t k=0;k<8;++k)
            reference.push_back({static_cast<double>(i),static_cast<double>(k)});
        const auto label="pattern_"+std::to_string(i);
        reusable.registerTemplate(label,reference);
        matcher.registerTemplate(label,std::move(reference));
    }
    constexpr std::size_t source_count=10;
    constexpr std::size_t warmup_frames=30;
    constexpr std::size_t measured_frames=100;
    std::size_t matches=0;
    for (std::size_t frame=0;frame<warmup_frames;++frame)
        for (std::size_t source=1;source<=source_count;++source) {
            const auto id=audition::AcousticSourceId{static_cast<std::uint64_t>(source)};
            const std::vector<double> sample{static_cast<double>(source%8),
                                             static_cast<double>(frame%8)};
            matches+=matcher.push(id,sample).size();
            reusable.pushInto(id,sample,reusable_output);
        }
    // Construct the input feature storage outside the measurement window.
    // A braced temporary vector passed to push() would itself allocate once.
    std::vector<double> input_frame(2);
    const auto before=allocations.load(std::memory_order_relaxed);
    std::size_t max_new_per_push=0;
    std::size_t zero_allocation_pushes=0;
    std::size_t matching_pushes=0;
    std::size_t matching_allocations=0;
    std::size_t nonmatching_allocations=0;
    std::size_t reuse_allocations=0;
    std::size_t reuse_max_new_per_push=0;
    std::size_t reuse_zero_allocation_pushes=0;
    std::size_t reuse_matching_allocations=0;
    std::size_t reuse_nonmatching_allocations=0;
    for (std::size_t frame=warmup_frames;frame<warmup_frames+measured_frames;++frame)
        for (std::size_t source=1;source<=source_count;++source) {
            const auto frame_before=allocations.load(std::memory_order_relaxed);
            input_frame[0]=static_cast<double>(source%8);
            input_frame[1]=static_cast<double>(frame%8);
            const auto result=matcher.push(audition::AcousticSourceId{static_cast<std::uint64_t>(source)},
                                          input_frame);
            const auto used=allocations.load(std::memory_order_relaxed)-frame_before;
            matches+=result.size();
            // Both matchers receive identical features and source histories.
            // Count the caller-owned output path separately from push().
            const auto reuse_before=allocations.load(std::memory_order_relaxed);
            reusable.pushInto(audition::AcousticSourceId{static_cast<std::uint64_t>(source)},
                              input_frame,reusable_output);
            const auto reuse_used=allocations.load(std::memory_order_relaxed)-reuse_before;
            reuse_allocations+=reuse_used;
            reuse_max_new_per_push=std::max(reuse_max_new_per_push,reuse_used);
            if (reuse_used==0) ++reuse_zero_allocation_pushes;
            if (result.size()!=reusable_output.size()) {
                std::cerr<<"push/pushInto result count mismatch at frame "<<frame<<"\n";
                return 2;
            }
            for (std::size_t k=0;k<result.size();++k) {
                if (result[k].label!=reusable_output[k].label ||
                    result[k].normalized_cost!=reusable_output[k].normalized_cost ||
                    result[k].ending_frame!=reusable_output[k].ending_frame) {
                    std::cerr<<"push/pushInto result mismatch at frame "<<frame<<"\n";
                    return 2;
                }
            }
            if (result.empty()) reuse_nonmatching_allocations+=reuse_used;
            else reuse_matching_allocations+=reuse_used;
            if (result.empty()) {
                nonmatching_allocations+=used;
            } else {
                ++matching_pushes;
                matching_allocations+=used;
            }
            if (used>max_new_per_push) max_new_per_push=used;
            if (used==0) ++zero_allocation_pushes;
        }
    const auto allocated=allocations.load(std::memory_order_relaxed)-before;
    const auto calls=source_count*measured_frames;
    std::cout<<"[temporal-stream-allocations] sources="<<source_count
             <<" templates=8 calls="<<calls
             <<" cpp_new_calls="<<allocated
             <<" new_calls_per_push="<<static_cast<double>(allocated)/static_cast<double>(calls)
             <<" max_cpp_new_per_push="<<max_new_per_push
             <<" zero_allocation_pushes="<<zero_allocation_pushes
             <<" matching_pushes="<<matching_pushes
             <<" matching_allocations="<<matching_allocations
             <<" nonmatching_allocations="<<nonmatching_allocations
             <<" matches="<<matches
             <<" reuse_cpp_new_calls="<<reuse_allocations
             <<" reuse_new_calls_per_push="<<static_cast<double>(reuse_allocations)/static_cast<double>(calls)
             <<" reuse_max_cpp_new_per_push="<<reuse_max_new_per_push
             <<" reuse_zero_allocation_pushes="<<reuse_zero_allocation_pushes
             <<" reuse_matching_allocations="<<reuse_matching_allocations
             <<" reuse_nonmatching_allocations="<<reuse_nonmatching_allocations
             <<" reuse_output_capacity="<<reusable_output.capacity()<<"\n";
    return 0;
}
