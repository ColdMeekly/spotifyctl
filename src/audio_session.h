#pragma once

// Windows Core Audio session for Spotify's process. Exposes per-app volume
// + mute control and a background poll that observes the peak meter to
// decide whether Spotify is currently audible.
//
// Internal-only header.

#include "impl.h"

#include <memory>

namespace spotify {

class AudioSession {
 public:
    explicit AudioSession(SpotifyClient::Impl* impl);
    ~AudioSession();

    AudioSession(const AudioSession&) = delete;
    AudioSession& operator=(const AudioSession&) = delete;

    void Start();
    void Stop();

    bool HasSession() const;
    float GetVolume() const;           // [0,1], -1 if unresolved
    bool  SetVolume(float v);
    bool  IsMuted() const;
    bool  SetMuted(bool muted);
    float GetPeakAmplitude() const;    // [0,1], -1 if unresolved

 private:
    struct Priv;
    std::unique_ptr<Priv> p_;
};

}  // namespace spotify
