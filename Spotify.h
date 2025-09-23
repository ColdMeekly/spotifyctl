#pragma once

#include <Windows.h>
#include <boost/signals2.hpp>
#include <string>
#include <tchar.h>

namespace bs2 = boost::signals2;

class Spotify
{
public:
    using EventOpened = bs2::signal<void(HWND hWnd, DWORD procId)>;
    using EventClosed = bs2::signal<void()>;
    using EventTitleChanged = bs2::signal<void(const std::wstring& newTitle)>;

    EventOpened OnOpened;
    EventClosed OnClosed;
    EventTitleChanged OnTitleChanged;

    static Spotify& getInstance()
    {
        static Spotify instance;
        return instance;
    }

    Spotify(Spotify const&) = delete;
    void operator=(Spotify const&) = delete;

    HWND GetWindow() { return m_window; }
    static bool IsSpotifyWindow(HWND hWnd);

    void Start();

    enum class AppCommands
    {
        Stop,
        Play,
        Pause,
        PlayPause,
        NextTrack,
        PrevTrack,
        VolUp,
        VolDown,
        MuteUnmute
    };

    bool Control(AppCommands cmd);
    bool SendKey(WPARAM keyCode, LPARAM keyInfo = 0);

private:
    bool m_isRunning = false;
    HWND m_window = NULL;
    DWORD m_processId = 0;
    HWINEVENTHOOK m_hookTitle;
    HWINEVENTHOOK m_hookWindow;
    std::wstring m_title;

    void SetWindow(HWND hWnd);

    static void WinEvent_NameEvent(
        HWINEVENTHOOK hWinEventHook,
        DWORD event,
        HWND hwnd,
        LONG idObject,
        LONG idChild,
        DWORD idEventThread,
        DWORD dwmsEventTime
    );

    static void WinEvent_WindowEvent(
        HWINEVENTHOOK hWinEventHook,
        DWORD event,
        HWND hwnd,
        LONG idObject,
        LONG idChild,
        DWORD idEventThread,
        DWORD dwmsEventTime
    );

    Spotify() = default;
    ~Spotify() = default;
};

