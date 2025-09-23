#include "Spotify.h"
#include <Psapi.h>

#define ITEM_SIZE(arr) sizeof(arr) / sizeof(arr[0])

bool Spotify::IsSpotifyWindow(HWND hWnd) {
    _TCHAR wndClassName[MAX_PATH]{ 0 };
    GetClassName(hWnd, wndClassName, ITEM_SIZE(wndClassName));

    if (!_tcscmp(wndClassName, _T("Chrome_WidgetWin_0")) &&
        GetWindowLong(hWnd, GWL_STYLE) & WS_VISIBLE) {
        DWORD process_id;
        GetWindowThreadProcessId(hWnd, &process_id);

        HANDLE hprocess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
        if (hprocess != NULL) {
            _TCHAR procFileName[MAX_PATH]{ 0 };
            GetModuleFileNameEx(hprocess, NULL, procFileName, MAX_PATH);
            CloseHandle(hprocess);

            _TCHAR* period = _tcsrchr(procFileName, '\\');

            if (period != NULL && !_tcscmp(period, _T("\\Spotify.exe"))) {
                return true;
            }
        }
    }
    return false;
}

void Spotify::SetWindow(HWND hWnd) {
    bool wasRunning = m_isRunning;
    m_window = hWnd;
    m_isRunning = m_window != NULL;
    m_processId = NULL;

    if (m_isRunning)
        GetWindowThreadProcessId(m_window, &m_processId);

    if (!wasRunning && m_isRunning) {
        OnOpened(m_window, m_processId);
    }
    else if (wasRunning && !m_isRunning) {
        OnClosed();
    }
}

bool Spotify::Control(AppCommands cmd) {
    static unsigned int AppCommandsList[] = {
        APPCOMMAND_MEDIA_STOP,
        APPCOMMAND_MEDIA_PLAY,
        APPCOMMAND_MEDIA_PAUSE,
        APPCOMMAND_MEDIA_PLAY_PAUSE,
        APPCOMMAND_MEDIA_NEXTTRACK,
        APPCOMMAND_MEDIA_PREVIOUSTRACK,
        APPCOMMAND_VOLUME_UP,
        APPCOMMAND_VOLUME_DOWN,
        APPCOMMAND_VOLUME_MUTE
    };

    if (!m_isRunning)
        return false;

    unsigned int code = AppCommandsList[(unsigned int)cmd];
    return PostMessage(m_window, WM_APPCOMMAND, FAPPCOMMAND_KEY, MAKELONG(0, code));
}

bool Spotify::SendKey(WPARAM keyCode, LPARAM keyInfo) {
    return PostMessage(m_window, WM_KEYDOWN, keyCode, keyInfo);
}

void Spotify::WinEvent_NameEvent(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd,
    LONG idObject, LONG idChild, DWORD idEventThread, DWORD dwmsEventTime) {
    LONG exstyle = GetWindowLong(hwnd, GWL_EXSTYLE);

    if (hwnd != NULL && event == EVENT_OBJECT_NAMECHANGE && exstyle & WS_EX_WINDOWEDGE) {
        _TCHAR spotifyWndText[255]{ 0 };
        GetWindowText(hwnd, &spotifyWndText[0], sizeof(spotifyWndText) / sizeof(_TCHAR) - 1);
        Spotify& spotify = Spotify::getInstance();

        spotify.OnTitleChanged(spotifyWndText);
    }
}

void Spotify::WinEvent_WindowEvent(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd,
    LONG idObject, LONG idChild, DWORD idEventThread, DWORD dwmsEventTime) {

    if (!IsWindow(hwnd))
        return;

    Spotify& spotify = Spotify::getInstance();
    if (hwnd != spotify.GetWindow() && !Spotify::IsSpotifyWindow(hwnd))
        return;

    if (event == EVENT_OBJECT_CREATE) {
        spotify.SetWindow(hwnd);
    }
    else if (event == EVENT_OBJECT_DESTROY) {
        spotify.SetWindow(NULL);
    }
}

void Spotify::Start()
{
    // Setup hooks first to make sure we don't miss spotify opening / closing
    m_hookWindow = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_DESTROY, NULL,
        				WinEvent_WindowEvent, NULL, NULL, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS | WINEVENT_SKIPOWNTHREAD);

    this->OnOpened.connect([this](HWND hWnd, DWORD procId)
    {
        m_hookTitle = SetWinEventHook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE, NULL,
        WinEvent_NameEvent, procId, NULL, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

        _TCHAR spotifyWndText[255]{ 0 };
        GetWindowText(hWnd, &spotifyWndText[0], sizeof(spotifyWndText) / sizeof(_TCHAR) - 1);
        this->OnTitleChanged(spotifyWndText);
    });

    // Loop over potential spotify windows and call SetWindow on the first one we find
    HWND hWnd = FindWindow(_T("Chrome_WidgetWin_0"), NULL);
    while (hWnd != NULL) {
        if (IsSpotifyWindow(hWnd)) {
			SetWindow(hWnd);
			break;
		}
		hWnd = FindWindowEx(NULL, hWnd, _T("Chrome_WidgetWin_0"), NULL);
	}
}