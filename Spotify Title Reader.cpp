#include "Spotify.h"
#include <shellapi.h>
#include <iostream>
#include <chrono>

#include <Commctrl.h>

#ifdef _UNICODE
#define TCOUT std::wcout
#else 
#define TCOUT std::cout
#endif // _UNICODE

using tstring = std::basic_string<TCHAR>;

int main()
{
    Spotify& spotify = Spotify::getInstance();

    spotify.OnOpened.connect([&spotify](HWND hWnd, DWORD procId) {
        TCOUT << _T("Ruh Roh! Guess who opened Spotify!!\n");
        ShellExecute(spotify.GetWindow(), _T("open"), _T("spotify:track:4cOdK2wGLETKBW3PvgPWqT"), nullptr, nullptr, SW_NORMAL);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        PostMessage(spotify.GetWindow(), WM_KEYDOWN, VK_RETURN, 0);
    });

    spotify.OnClosed.connect([]() {
        TCOUT << _T("I'll be waiting!\n");
    });

    static tstring lastTitle;

    spotify.OnTitleChanged.connect([&spotify](const tstring& newTitle) {
        //TCOUT << _T("Title: ") << newTitle << _T('\n');

        if (newTitle == _T("Spotify Free") || newTitle == _T("Spotify Premium")) // aka paused - need better way of checking
        {
            if (!lastTitle.empty() && lastTitle == _T("Rick Astley - Never Gonna Give You Up"))
            {
                //TCOUT << _T("Oh hey, Rick Astley is playing!\n");
                TCOUT << _T("Tab Idx: ") << SendMessage(spotify.GetWindow(), TCM_GETCURSEL, 0, 0) << _T('\n');
                spotify.Control(Spotify::AppCommands::Play);
                return;
            }
        }
        else {
            lastTitle = newTitle;

            if (!lastTitle.empty() && lastTitle != _T("Rick Astley - Never Gonna Give You Up"))
            {
                //TCOUT << _T("Rick will never give you up. You will never give him up >:)\n");
                ShellExecute(spotify.GetWindow(), _T("open"), _T("spotify:track:4cOdK2wGLETKBW3PvgPWqT"), nullptr, nullptr, SW_NORMAL);
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                PostMessage(spotify.GetWindow(), WM_KEYDOWN, VK_RETURN, 0);
                return;
            }
        }
    });

    spotify.Start();

    MSG msg;
    while (GetMessage(&msg, nullptr, NULL, NULL)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
