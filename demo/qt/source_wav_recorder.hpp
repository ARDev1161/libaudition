#pragma once
#include <audition/recording/source_wav_recorder.hpp>

// Compatibility adapter for older demo-only tests and consumers.
namespace demo { using SourceWavRecorder = audition::recording::SourceWavRecorder; }
