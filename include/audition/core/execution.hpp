#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace audition {

enum class DeviceClass {
    Auto,
    Cpu,
    Gpu,
    Npu,
    Accelerator,
};

enum class PrecisionPreference {
    Auto,
    Float32,
    Float16,
    Int8,
};

struct ExecutionTarget {
    DeviceClass device_class{DeviceClass::Auto};
    std::string provider{};       // e.g. "cuda", "rknn", "qnn", "openvino"
    std::int32_t device_index{-1};
    PrecisionPreference precision{PrecisionPreference::Auto};
    bool allow_fallback{true};
    std::map<std::string, std::string> provider_options{};
};

}  // namespace audition
