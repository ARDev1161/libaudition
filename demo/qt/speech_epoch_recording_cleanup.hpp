#pragma once

#include <audition/recording/source_recording_session.hpp>

#include <cstdint>
#include <map>
#include <set>
#include <utility>

namespace demo {

// Capture-thread bookkeeping shared by ALSA integration and its regression
// tests. The worker's Forget acknowledgement is an invalidation boundary:
// a reused source can already have a Bound acknowledgement at this epoch.
using SpeechEpochRecordingMap =
    std::map<std::pair<std::uint64_t,std::uint64_t>,audition::RecordingId>;

inline void clearForgottenSpeechRecordingEpochs(
    SpeechEpochRecordingMap& epochs,
    std::set<audition::RecordingId>& deferred_ack_recordings,
    audition::recording::SourceRecordingSession& recordings,
    std::uint64_t source_id,std::uint64_t forget_epoch) {
    auto it=epochs.lower_bound({source_id,0U});
    while (it!=epochs.end() && it->first.first==source_id &&
           it->first.second<forget_epoch) {
        // An old Retire can be invalidated before its worker ack. Persist
        // the retired WAV without speech, but never touch the reused ID's
        // current/future epoch or any other source.
        if (deferred_ack_recordings.erase(it->second))
            (void)recordings.tryAcknowledgeRetired(it->second,std::nullopt);
        it=epochs.erase(it);
    }
}

} // namespace demo
