#pragma once

#include <audition/backends/odas/odas_options.hpp>

extern "C" {
#include <odas/odas.h>
}

namespace audition::odas_detail {

[[nodiscard]] mod_ssl_cfg* makeSslConfig(const OdasOptions& options);
[[nodiscard]] mod_sst_cfg* makeSstConfig(const OdasOptions& options);
[[nodiscard]] mod_noise_cfg* makeNoiseConfig(const OdasOptions& options);
[[nodiscard]] mod_sss_cfg* makeSssConfig(const OdasOptions& options);

}  // namespace audition::odas_detail
