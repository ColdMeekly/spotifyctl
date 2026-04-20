#pragma once

// Pure-function parser for Spotify desktop window titles. Lives in the
// public include tree so consumers (and tests) can use it standalone.

#include <string>
#include <string_view>

#include "spotify/playback.h"

namespace spotify {

struct TitleParse {
    // True when the title represents "no track currently loaded / idle":
    //   "Spotify", "Spotify Free", "Spotify Premium"
    bool idle = false;

    // True when Spotify is playing an advertisement (title == "Advertisement").
    bool isAd = false;

    // Non-empty only for titles of the form "Artist - Track".
    std::string artist;
    std::string title;

    // Derived hint. Unknown for ads / parsed tracks (SMTC is authoritative
    // for play/pause); Paused when the title is an idle sentinel.
    PlaybackState::Status status = PlaybackState::Status::Unknown;
};

TitleParse ParseWindowTitle(std::string_view rawTitle);

}  // namespace spotify
