// Never gonna give you up: when Spotify opens (or plays anything else),
// hijack it with a specific Rick Astley track.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "spotify/client.h"

namespace {
constexpr const char* kRickAstleyTrackId = "4cOdK2wGLETKBW3PvgPWqT";
constexpr const char* kRickAstleyTitle   = "Rick Astley - Never Gonna Give You Up";

void KickOffRickroll(spotify::SpotifyClient& client) {
    client.OpenUri(spotify::uri::Track(kRickAstleyTrackId));
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    client.SendKey(VK_RETURN);
}
}  // namespace

int main() {
    spotify::SpotifyClient client;

    client.OnOpened.connect([&client] {
        std::cout << "Ruh Roh! Guess who opened Spotify!!\n";
        KickOffRickroll(client);
    });

    client.OnStateChanged.connect([](const spotify::PlaybackState& state) {
        std::cout << "State changed: " << state.artist << " - " << state.title
                  << " [" << state.position.count() / 1000 << "/"
			<< state.duration.count() / 1000 << "]\n";
		});

    client.OnClosed.connect([] {
        std::cout << "I'll be waiting!\n";
    });

    static std::string lastTitle;
    client.OnRawTitle.connect([&client](const std::string& newTitle) {
        const bool idle = (newTitle == "Spotify Free" || newTitle == "Spotify Premium");
        if (idle) {
            if (lastTitle == kRickAstleyTitle) {
                client.SendCommand(spotify::AppCommand::Play);
            }
            return;
        }
        lastTitle = newTitle;
        if (!lastTitle.empty() && lastTitle != kRickAstleyTitle) {
            KickOffRickroll(client);
        }
    });

    client.Start();

    MSG msg;
    while (::GetMessageW(&msg, nullptr, 0, 0)) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    return 0;
}
