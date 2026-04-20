#pragma once

// Internal-only header. Pure, Windows-free fusion of the three per-source
// fragments (SMTC, Core Audio, Title) into the unified PlaybackState that the
// client publishes through OnStateChanged / LatestState.
//
// Extracted from Impl::Aggregate so the merging rules can be unit-tested
// without spinning up Windows hooks.

#include <string>
#include <utility>

#include "spotify/playback.h"

namespace spotify {

struct AudioFrag {
    float vol      = -1.0f;
    bool  muted    = false;
    bool  audible  = false;
    bool  resolved = false;  // audio session bound at least once
};

struct TitleFrag {
    bool                   idle   = false;
    bool                   isAd   = false;
    std::string            artist;
    std::string            title;
    PlaybackState::Status  status = PlaybackState::Status::Unknown;
    bool                   any    = false;  // has ApplyTitle ever run?
};

// Precedence for artist / title / album / position / duration / art / caps:
//     SMTC  >  Title
// Audio layer owns:  audible, appVolume, appMuted.
// Title layer owns:  isAd detection; provides fallback artist/title and a
// status hint when SMTC is silent.
inline PlaybackState FuseFragments(const PlaybackState& smtc,
                                   const AudioFrag&     audio,
                                   const TitleFrag&     title) {
    PlaybackState out;

    const bool smtcHasContent =
        !smtc.title.empty() || !smtc.artist.empty() ||
        smtc.status != PlaybackState::Status::Unknown;

    if (smtcHasContent) {
        out = smtc;  // copies art bytes too
    }

    if (out.artist.empty() && !title.artist.empty()) out.artist = title.artist;
    if (out.title.empty()  && !title.title.empty())  out.title  = title.title;
    if (out.status == PlaybackState::Status::Unknown && title.any) {
        out.status = title.status;
    }
    out.isAd = title.isAd;

    out.appVolume = audio.resolved ? audio.vol : -1.0f;
    out.appMuted  = audio.muted;
    out.audible   = audio.audible;
    return out;
}

}  // namespace spotify
