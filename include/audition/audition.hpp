#pragma once

#include <audition/core.hpp>
#include <audition/interfaces.hpp>
#include <audition/dsp/basic.hpp>
#include <audition/dsp/audio_frontend.hpp>
#include <audition/dsp/quality.hpp>
#include <audition/logging/logging.hpp>
#include <audition/memory/in_memory_registries.hpp>
#include <audition/memory/sound_prototype_matcher.hpp>
#include <audition/memory/source_identity_resolver.hpp>
#include <audition/memory/types.hpp>
#include <audition/model/in_memory_model_registry.hpp>
#include <audition/model/model_descriptor.hpp>
#include <audition/pipeline.hpp>
#include <audition/registry/factory_registry.hpp>
#include <audition/spatial/level_range_prior.hpp>
#include <audition/spatial/temporal_smoother.hpp>
