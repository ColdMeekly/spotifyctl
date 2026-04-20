#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <string_view>

#include "spotify/control.h"
#include "spotify/events.h"
#include "spotify/playback.h"

namespace spotify {

// SpotifyClient observes and controls the local Spotify desktop app without
// touching the Spotify Web API. Construct one, connect signals, then Start().
//
// Data sources (layered; more specific wins):
//   1. Window title hook  (always available)
//   2. Windows SMTC       (Win10 1809+; rich metadata, position, art)
//   3. Windows Core Audio (per-app volume + audible detection)
//
// Threading contract: signals may fire on a Windows-owned background thread
// (WinEvent dispatcher, SMTC async thread, or the audio-poll thread). Do not
// block inside slots; marshal to your own loop if needed.
class SpotifyClient {
 public:
    SpotifyClient();
    ~SpotifyClient();

    SpotifyClient(const SpotifyClient&) = delete;
    SpotifyClient& operator=(const SpotifyClient&) = delete;
    SpotifyClient(SpotifyClient&&) = delete;
    SpotifyClient& operator=(SpotifyClient&&) = delete;

    // Installs hooks and scans for an already-running Spotify window.
    // Idempotent.
    void Start();

    // Removes hooks and releases resources. Idempotent.
    void Stop();

    // True when the Spotify desktop window is currently tracked.
    bool IsRunning() const;

    // Events (all strings are UTF-8).
    Signal<void()>                       OnOpened;
    Signal<void()>                       OnClosed;
    Signal<void(const std::string&)>     OnRawTitle;        // raw window title
    Signal<void(const PlaybackState&)>   OnStateChanged;    // unified state
    Signal<void(bool /*audible*/)>       OnAudibleChanged;  // audio output edge

    // Track-edge signals. OnTrackChanged fires whenever the (artist, title,
    // album) tuple changes, including empty→populated at startup. OnAdStarted /
    // OnAdEnded fire on the isAd flag flipping false→true / true→false. All
    // three fire on the same Windows background thread as OnStateChanged and
    // are always fired BEFORE the matching OnStateChanged emission.
    Signal<void(const PlaybackState& /*previous*/,
                const PlaybackState& /*current*/)> OnTrackChanged;
    Signal<void()>                                 OnAdStarted;
    Signal<void()>                                 OnAdEnded;

    // Ticks ~1 Hz while playback is Playing AND at least one slot is
    // connected. Payload is the smooth position (same value as
    // LatestPositionSmooth()). Zero cost when nobody is listening.
    Signal<void(std::chrono::milliseconds)>        OnPositionChanged;

    // Snapshot of the latest unified state.
    PlaybackState LatestState() const;

    // Current position with monotonic-clock-based extrapolation applied when
    // playback is Playing. When Paused/Stopped, returns the raw anchor
    // position. Consumers building progress bars should poll this rather than
    // reimplement `position + (now - anchor)` on top of OnStateChanged.
    std::chrono::milliseconds LatestPositionSmooth() const;

    // ---- Controls -----------------------------------------------------
    // All return false if Spotify is not currently running or the channel
    // is unavailable (e.g. SMTC not yet resolved).

    // Media-key style command (fast, works while minimized).
    bool SendCommand(AppCommand cmd);

    // SMTC-based transport controls. Preferred for Seek and when a specific
    // verb is wanted rather than a toggle.
    bool Play();
    bool Pause();
    bool Next();
    bool Previous();
    bool Seek(std::chrono::milliseconds position);

    // Launches a `spotify:...` URI via ShellExecute. Does not wait for
    // navigation to complete.
    bool OpenUri(std::string_view spotifyUri);

    // Low-level WM_KEYDOWN injection. Accepts a Windows virtual-key code.
    bool SendKey(unsigned virtualKey);

    // ---- Per-app audio (Core Audio) ----------------------------------
    // Return -1 / false while the session hasn't been resolved yet.

    float GetAppVolume() const;   // [0,1] or -1
    bool  SetAppVolume(float v);  // false if session not resolved
    bool  IsAppMuted() const;
    bool  SetAppMuted(bool muted);
    float GetPeakAmplitude() const;  // [0,1] or -1

    // Opaque implementation type. Declared here (rather than in a private
    // section) so internal translation units can name `SpotifyClient::Impl`
    // without needing friend declarations. The definition is deliberately
    // kept out of this header — users cannot construct or inspect it.
    struct Impl;

 private:
    std::unique_ptr<Impl> m_;
};

}  // namespace spotify
