#pragma once

// Windows SMTC (GlobalSystemMediaTransportControlsSessionManager) source.
// Internal-only header.

#include "impl.h"

#include <chrono>
#include <memory>

namespace spotify {

class SmtcSource {
 public:
    explicit SmtcSource(SpotifyClient::Impl* impl);
    ~SmtcSource();

    SmtcSource(const SmtcSource&) = delete;
    SmtcSource& operator=(const SmtcSource&) = delete;

    void Start();
    void Stop();

    // True when a Spotify SMTC session is currently resolved (i.e. Spotify
    // has produced media info since boot). False during startup and after
    // Spotify closes.
    bool HasSession() const;

    // Transport controls via SMTC's Try...Async. Return false synchronously
    // when there is no session; otherwise the async result is fire-and-forget.
    bool Play();
    bool Pause();
    bool Next();
    bool Previous();
    bool Seek(std::chrono::milliseconds position);

 private:
    struct Priv;
    std::unique_ptr<Priv> p_;
};

}  // namespace spotify
