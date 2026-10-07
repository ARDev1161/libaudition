#pragma once

#include <cstddef>
#include <vector>

#include <audition/backends/odas/odas_options.hpp>

namespace audition::odas_detail {

[[nodiscard]] std::vector<std::size_t> resolvedInputChannels(const OdasOptions& options);

}  // namespace audition::odas_detail
