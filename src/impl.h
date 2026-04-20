#pragma once

// Internal-only header. Must not be installed or included from public headers.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include "spotify/client.h"
#include "spotify/playback.h"
#include "spotify/title_parser.h"

namespace spotify {

class SmtcSource;    // defined in src/smtc_source.h   (Phase 3)
class AudioSession;  // defined in src/audio_session.h (Phase 4)

struct SpotifyClient::Impl {
    SpotifyClient* self = nullptr;

    std::atomic<bool> hooksInstalled{false};

    // Current tracked Spotify main window. nullptr when Spotify isn't open.
    HWND  window = nullptr;
    DWORD processId = 0;

    HWINEVENTHOOK hookWindow = nullptr;
    HWINEVENTHOOK hookTitle  = nullptr;

    mutable std::mutex stateMu;
    PlaybackState latest;  // published unified state

    // Per-source fragments. Aggregator merges these into `latest`.
    mutable std::mutex fragMu;
    PlaybackState smtcFrag;   // artist/title/album/position/duration/art/status/caps
    struct {
        float vol = -1.0f;
        bool  muted = false;
        bool  audible = false;
        bool  resolved = false;
    } audioFrag;
    struct {
        bool idle = false;
        bool isAd = false;
        std::string artist;
        std::string title;
        PlaybackState::Status status = PlaybackState::Status::Unknown;
        bool any = false;  // has ApplyTitle ever been called?
    } titleFrag;

    std::unique_ptr<SmtcSource>   smtc;
    std::unique_ptr<AudioSession> audio;

    Impl();
    ~Impl();

    // Called under the WinEvent thread to flip between "no spotify" and
    // "we have a spotify window". Fires OnOpened / OnClosed accordingly.
    void SetWindow(HWND hWnd);

    // Per-source fragment ingestion. Each call re-runs Aggregate() and
    // publishes a new unified state if anything changed.
    void ApplySmtc(const PlaybackState& frag);
    void ApplyAudio(float vol, bool muted, bool audible, bool resolved);
    void ApplyTitle(std::string_view rawTitle);
    void ResetFragments();

    // Merges fragments → latest, publishes via OnStateChanged when changed.
    void Aggregate();

    static void CALLBACK OnWindowEvent(HWINEVENTHOOK, DWORD event, HWND hwnd,
                                       LONG idObject, LONG idChild,
                                       DWORD idEventThread, DWORD dwmsEventTime);

    static void CALLBACK OnNameEvent(HWINEVENTHOOK, DWORD event, HWND hwnd,
                                     LONG idObject, LONG idChild,
                                     DWORD idEventThread, DWORD dwmsEventTime);
};

// UTF-8 <-> UTF-16 helpers, shared across translation units.
std::string  WideToUtf8(std::wstring_view w);
std::wstring Utf8ToWide(std::string_view s);

bool IsSpotifyWindow(HWND hWnd);

}  // namespace spotify
