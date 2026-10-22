#include <audition/temporal/acoustic_motif_finder.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>

// Isolated from GoogleTest: global new/new[] instrumentation affects only
// this process. Counts C++ allocations, not malloc, bytes or system calls.
namespace {
std::atomic<std::size_t> new_calls{0};
}
void* operator new(std::size_t bytes) {
    if (void* p=std::malloc(bytes?bytes:1)) {
        new_calls.fetch_add(1,std::memory_order_relaxed);
        return p;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) {
    if (void* p=std::malloc(bytes?bytes:1)) {
        new_calls.fetch_add(1,std::memory_order_relaxed);
        return p;
    }
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}

int main() {
    audition::temporal::MotifOptions opts{};
    opts.max_frames=48;
    opts.max_dimensions=4;
    opts.window_frames=4;
    opts.stride=1;
    opts.warping_radius=1;
    opts.max_normalized_cost=0;
    audition::temporal::AcousticMotifFinder finder{opts};
    std::vector<std::vector<double>> frames;
    frames.reserve(32);
    for (std::size_t i=0;i<32;++i) {
        const auto value=static_cast<double>(i);
        frames.push_back({value,value+1,value+2,value+3});
    }
    for (std::size_t i=0;i<4;++i)
        frames[12+i]=frames[i];
    if (!finder.find(frames).found || finder.findMultiple(frames,2).empty()) {
        std::cerr<<"motif allocation workload must contain a recognized motif\n";
        return 2;
    }

    constexpr std::size_t repetitions=8;
    std::size_t single_allocations=0;
    std::size_t one_allocations=0;
    std::size_t multiple_allocations=0;
    std::chrono::steady_clock::duration single_elapsed{};
    std::chrono::steady_clock::duration one_elapsed{};
    std::chrono::steady_clock::duration multiple_elapsed{};
    for (std::size_t i=0;i<repetitions;++i) {
        auto before=new_calls.load(std::memory_order_relaxed);
        auto start=std::chrono::steady_clock::now();
        const auto result=finder.find(frames);
        single_elapsed+=std::chrono::steady_clock::now()-start;
        single_allocations+=new_calls.load(std::memory_order_relaxed)-before;
        if (!result.found || result.first_start!=0 || result.second_start!=12)
            return 3;

        before=new_calls.load(std::memory_order_relaxed);
        start=std::chrono::steady_clock::now();
        const auto one=finder.findMultiple(frames,1);
        one_elapsed+=std::chrono::steady_clock::now()-start;
        one_allocations+=new_calls.load(std::memory_order_relaxed)-before;
        if (one.size()!=1 || one.front().first_start!=result.first_start ||
            one.front().second_start!=result.second_start ||
            one.front().normalized_cost!=result.normalized_cost)
            return 5;

        before=new_calls.load(std::memory_order_relaxed);
        start=std::chrono::steady_clock::now();
        const auto results=finder.findMultiple(frames,2);
        multiple_elapsed+=std::chrono::steady_clock::now()-start;
        multiple_allocations+=new_calls.load(std::memory_order_relaxed)-before;
        if (results.empty() || results.front().first_start!=0 ||
            results.front().second_start!=12)
            return 4;
    }
    // Dense exact repetition deliberately exercises the worst-case
    // candidate-storage pressure of the general greedy selector.
    const std::vector<std::vector<double>> repeated(32,
        std::vector<double>{1.0,2.0,3.0,4.0});
    std::size_t dense_two_new_calls=0;
    std::size_t dense_three_new_calls=0;
    std::chrono::steady_clock::duration dense_two_elapsed{};
    std::chrono::steady_clock::duration dense_three_elapsed{};
    for (std::size_t i=0;i<repetitions;++i) {
        auto before=new_calls.load(std::memory_order_relaxed);
        auto start=std::chrono::steady_clock::now();
        const auto two=finder.findMultiple(repeated,2);
        dense_two_elapsed+=std::chrono::steady_clock::now()-start;
        dense_two_new_calls+=new_calls.load(std::memory_order_relaxed)-before;
        if (two.size()!=2 || two[0].first_start!=0 || two[0].second_start!=4 ||
            two[1].first_start!=8 || two[1].second_start!=12)
            return 6;

        before=new_calls.load(std::memory_order_relaxed);
        start=std::chrono::steady_clock::now();
        const auto three=finder.findMultiple(repeated,3);
        dense_three_elapsed+=std::chrono::steady_clock::now()-start;
        dense_three_new_calls+=new_calls.load(std::memory_order_relaxed)-before;
        if (three.size()!=3 || three[0].first_start!=0 || three[0].second_start!=4 ||
            three[1].first_start!=8 || three[1].second_start!=12 ||
            three[2].first_start!=16 || three[2].second_start!=20)
            return 7;
    }
    std::cout<<"[temporal-motif-dense] frames=32 window=4"
             <<" repetitions="<<repetitions
             <<" two_new_calls_per_invocation="<<
                 static_cast<double>(dense_two_new_calls)/static_cast<double>(repetitions)
             <<" three_new_calls_per_invocation="<<
                 static_cast<double>(dense_three_new_calls)/static_cast<double>(repetitions)
             <<" two_mean_us="<<std::chrono::duration<double,std::micro>(dense_two_elapsed).count()/
                 static_cast<double>(repetitions)
             <<" three_mean_us="<<std::chrono::duration<double,std::micro>(dense_three_elapsed).count()/
                 static_cast<double>(repetitions)
             <<"\n";
    std::cout<<"[temporal-motif-allocations] frames=32 dimensions=4 window=4"
             <<" repetitions="<<repetitions
             <<" find_new_calls="<<single_allocations
             <<" find_new_calls_per_invocation="<<
                 static_cast<double>(single_allocations)/static_cast<double>(repetitions)
             <<" find_multiple_one_new_calls="<<one_allocations
             <<" find_multiple_one_new_calls_per_invocation="<<
                 static_cast<double>(one_allocations)/static_cast<double>(repetitions)
             <<" find_multiple_new_calls="<<multiple_allocations
             <<" find_multiple_new_calls_per_invocation="<<
                 static_cast<double>(multiple_allocations)/static_cast<double>(repetitions)
             <<" find_mean_us="<<std::chrono::duration<double,std::micro>(single_elapsed).count()/
                 static_cast<double>(repetitions)
             <<" find_multiple_one_mean_us="<<
                 std::chrono::duration<double,std::micro>(one_elapsed).count()/
                 static_cast<double>(repetitions)
             <<" find_multiple_mean_us="<<
                 std::chrono::duration<double,std::micro>(multiple_elapsed).count()/
                 static_cast<double>(repetitions)
             <<"\n";
    return 0;
}
