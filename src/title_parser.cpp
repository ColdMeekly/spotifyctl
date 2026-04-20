#include "spotify/title_parser.h"

namespace spotify {

namespace {

constexpr std::string_view kSeparator = " - ";

std::string_view TrimSpaces(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' '  || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

}  // namespace

TitleParse ParseWindowTitle(std::string_view raw) {
    TitleParse out;
    const std::string_view trimmed = TrimSpaces(raw);

    if (trimmed.empty()) return out;

    if (trimmed == "Spotify" ||
        trimmed == "Spotify Free" ||
        trimmed == "Spotify Premium") {
        out.idle = true;
        out.status = PlaybackState::Status::Paused;
        return out;
    }

    if (trimmed == "Advertisement") {
        out.isAd = true;
        out.status = PlaybackState::Status::Playing;
        return out;
    }

    const auto sepPos = trimmed.find(kSeparator);
    if (sepPos != std::string_view::npos) {
        const auto artist = TrimSpaces(trimmed.substr(0, sepPos));
        const auto title  = TrimSpaces(trimmed.substr(sepPos + kSeparator.size()));
        if (!artist.empty() && !title.empty()) {
            out.artist.assign(artist);
            out.title.assign(title);
            out.status = PlaybackState::Status::Playing;
            return out;
        }
    }

    // Unrecognized title (e.g., translated / localized idle string we don't
    // know about). Leave everything default so the aggregator falls back to
    // whatever other sources report.
    return out;
}

}  // namespace spotify
