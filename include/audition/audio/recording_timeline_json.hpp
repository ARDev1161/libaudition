#pragma once
#include <audition/audio/recording_timeline.hpp>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>

namespace audition {
namespace detail {
inline void appendJsonString(std::ostream& out, const std::string& value) {
    out << '"';
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    out << "\\u00" << hex[c >> 4] << hex[c & 0xf];
                } else out << static_cast<char>(c);
        }
    }
    out << '"';
}
}
inline std::string recordingTimelineToJson(const RecordingTimeline& timeline) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17);
    out << "{\n  \"schema\": \"libaudition.recording-timeline.v1\",\n"
        << "  \"recording_id\": " << timeline.recordingId()
        << ",\n  \"sample_rate_hz\": " << timeline.sampleRateHz()
        << ",\n  \"speech\": [";
    bool first = true;
    for (const auto& event : timeline.speech()) {
        if (!first) out << ',';
        first = false;
        out << "\n    {\"start_sample\":" << event.start_sample
            << ",\"end_sample\":" << event.end_sample << ",\"text\":";
        detail::appendJsonString(out, event.text);
        out << ",\"language\":";
        detail::appendJsonString(out, event.language);
        out << '}';
    }
    out << "\n  ],\n  \"sounds\": [";
    first = true;
    for (const auto& event : timeline.sounds()) {
        if (!first) out << ',';
        first = false;
        out << "\n    {\"start_sample\":" << event.start_sample
            << ",\"end_sample\":" << event.end_sample << ",\"label\":";
        detail::appendJsonString(out, event.label);
        out << ",\"model\":";
        detail::appendJsonString(out, event.model);
        out << ",\"confidence\":" << event.confidence << ",\"stage\":";
        detail::appendJsonString(out, event.stage);
        out << '}';
    }
    out << "\n  ]\n}\n";
    return out.str();
}
} // namespace audition
