#include <audition/temporal/streaming_pattern_matcher.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <iostream>
#include <vector>

TEST(StreamingPatternBenchmark, MultisourceMultitemplateThroughputSmoke) {
    audition::temporal::StreamingPatternOptions options{};
    options.max_sources=10;
    options.max_templates=8;
    options.max_frames=24;
    options.max_dimensions=8;
    options.min_candidate_frames=6;
    options.max_candidate_frames=10;
    options.warping_radius=4;
    audition::temporal::StreamingPatternMatcher matcher{options};
    for (std::size_t label=0;label<8;++label) {
        std::vector<std::vector<double>> sample;
        for (std::size_t step=0;step<8;++step)
            sample.push_back({static_cast<double>(label),
                              static_cast<double>(step)});
        matcher.registerTemplate("pattern_"+std::to_string(label),std::move(sample));
    }
    constexpr std::size_t sources=10;
    constexpr std::size_t frames=100;
    std::size_t total_matches=0;
    std::chrono::steady_clock::duration worst_push{};
    const auto start=std::chrono::steady_clock::now();
    for (std::size_t frame=0;frame<frames;++frame) {
        for (std::size_t source=1;source<=sources;++source) {
            const auto push_start=std::chrono::steady_clock::now();
            const auto matches=matcher.push(audition::AcousticSourceId{
                static_cast<std::uint64_t>(source)},
                {static_cast<double>(source%8),static_cast<double>(frame%8)});
            worst_push=std::max(worst_push,std::chrono::steady_clock::now()-push_start);
            total_matches+=matches.size();
        }
    }
    const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now()-start).count();
    EXPECT_LE(total_matches,sources*frames*8);
    std::cout<<"[temporal-stream-benchmark] sources="<<sources
             <<" templates=8 frames_per_source="<<frames
             <<" push_calls="<<sources*frames
             <<" total_us="<<elapsed
             <<" mean_us_per_push="<<static_cast<double>(elapsed)/(sources*frames)
             <<" worst_push_us="<<std::chrono::duration<double,std::micro>(worst_push).count()
             <<" matches="<<total_matches<<"\n";
    // Performance is informational: noisy hosted CI runners must not gate
    // correctness with a machine-dependent latency threshold.
}

TEST(StreamingPatternBenchmark, TemplateAndSourceScalingMeasurements) {
    for (const std::size_t source_count : {std::size_t{1},std::size_t{5},std::size_t{10}}) {
        for (const std::size_t template_count : {std::size_t{1},std::size_t{4},std::size_t{8}}) {
            audition::temporal::StreamingPatternOptions options{};
            options.max_sources=source_count;
            options.max_templates=template_count;
            options.max_frames=24;
            options.min_candidate_frames=6;
            options.max_candidate_frames=10;
            audition::temporal::StreamingPatternMatcher matcher{options};
            for (std::size_t label=0;label<template_count;++label) {
                std::vector<std::vector<double>> sample;
                for (std::size_t k=0;k<8;++k)
                    sample.push_back({static_cast<double>(label),static_cast<double>(k)});
                matcher.registerTemplate("pattern_"+std::to_string(label),std::move(sample));
            }
            constexpr std::size_t frames_per_source=40;
            std::chrono::steady_clock::duration worst_push{};
            const auto start=std::chrono::steady_clock::now();
            std::size_t matches=0;
            for (std::size_t frame=0;frame<frames_per_source;++frame)
                for (std::size_t source=1;source<=source_count;++source) {
                    const auto begin=std::chrono::steady_clock::now();
                    const auto result=matcher.push(
                        audition::AcousticSourceId{static_cast<std::uint64_t>(source)},
                        {static_cast<double>(source%8),static_cast<double>(frame%8)});
                    worst_push=std::max(worst_push,std::chrono::steady_clock::now()-begin);
                    matches+=result.size();
                }
            const auto elapsed=std::chrono::duration<double,std::micro>(
                std::chrono::steady_clock::now()-start).count();
            const auto calls=source_count*frames_per_source;
            EXPECT_LE(matches,calls*template_count);
            std::cout<<"[temporal-stream-scaling] sources="<<source_count
                     <<" templates="<<template_count
                     <<" calls="<<calls
                     <<" mean_us="<<elapsed/static_cast<double>(calls)
                     <<" worst_us="<<std::chrono::duration<double,std::micro>(worst_push).count()
                     <<" matches="<<matches<<"\n";
        }
    }
}
