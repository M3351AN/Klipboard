// Copyright (c) 2025 渟雲. All rights reserved.

#include <windows.h>
#include <shellapi.h>
#include <stdio.h>

enum {
    kTrayIconMessage = WM_USER + 1,
    kTrayExitId = 1001,
    kTrayChangeHotkeyId = 1002
};

static HHOOK g_keyboard_hook;
static int g_use_hotkey_fallback = 0;
static UINT g_current_virtual_key = VK_F8;  // default hotkey F8
static char g_ini_file_path[MAX_PATH];
static int g_is_capturing_hotkey = 0;
static HWND g_main_window_handle;

static void LoadConfiguration() {
    GetModuleFileNameA(NULL, g_ini_file_path, MAX_PATH);
    char* last_backslash = strrchr(g_ini_file_path, '\\');
    if (last_backslash) *(last_backslash + 1) = '\0';
    snprintf(g_ini_file_path, sizeof(g_ini_file_path), "%s%s",
         g_ini_file_path, "klipboard_config.ini");


    g_current_virtual_key = GetPrivateProfileIntA("Settings", "HotkeyVK",
         VK_F8, g_ini_file_path);
}

static void SaveConfiguration() {
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%u", g_current_virtual_key);
    WritePrivateProfileStringA("Settings", "HotkeyVK", buffer, g_ini_file_path);
}

static void PasteClipboardText() {
    if (!OpenClipboard(NULL)) return;

    HANDLE clipboard_data = GetClipboardData(CF_UNICODETEXT);
    if (!clipboard_data) {
        CloseClipboard();
        return;
    }

    wchar_t* text = (wchar_t*)GlobalLock(clipboard_data);
    if (!text) {
        CloseClipboard();
        return;
    }

    INPUT input;
    for (wchar_t* character = text; *character; character++) {
        ZeroMemory(&input, sizeof(INPUT));
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = 0;
        input.ki.wScan = *character;
        input.ki.dwFlags = KEYEVENTF_UNICODE;
        SendInput(1, &input, sizeof(INPUT));

        input.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        SendInput(1, &input, sizeof(INPUT));
    }

    GlobalUnlock(clipboard_data);
    CloseClipboard();
}

void ShowTrayBalloon(LPCSTR title, LPCSTR text) {
    NOTIFYICONDATA nid = {0};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_main_window_handle;
    nid.uID = 1;
    nid.uFlags = NIF_INFO;
    strncpy(nid.szInfoTitle, title, sizeof(nid.szInfoTitle)-1);
    strncpy(nid.szInfo, text, sizeof(nid.szInfo)-1);
    nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

static LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wparam,
    LPARAM lparam) {
    if (code == HC_ACTION) {
        KBDLLHOOKSTRUCT* keyboard_data = (KBDLLHOOKSTRUCT*)lparam;

        if (g_is_capturing_hotkey && wparam == WM_KEYDOWN) {
            g_current_virtual_key = keyboard_data->vkCode;
            SaveConfiguration();
            g_is_capturing_hotkey = 0;
            ShowTrayBalloon("Change Hotkey", "Hotkey changed");

            if (g_use_hotkey_fallback) {
                UnregisterHotKey(g_main_window_handle, 1);
                RegisterHotKey(g_main_window_handle, 1, 0,
                    g_current_virtual_key);
            }
            return 1;  // prevent hotkeys captured by other programs,
                       // after all, user need only to paste
        }

        if (!g_is_capturing_hotkey && wparam == WM_KEYDOWN &&
            keyboard_data->vkCode == g_current_virtual_key) {
            PasteClipboardText();
            return 1;
        }
    }
    return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
}

static void ChangeHotkey(HWND window_handle) {
    g_is_capturing_hotkey = 1;
    ShowTrayBalloon("Change Hotkey", "Press new hotkey");
}

static void ShowTrayMenu(HWND window_handle) {
    POINT cursor_position;
    GetCursorPos(&cursor_position);

    HMENU menu = CreatePopupMenu();
    InsertMenu(menu, -1, MF_BYPOSITION, kTrayChangeHotkeyId, "Change Hotkey");
    InsertMenu(menu, -1, MF_BYPOSITION, kTrayExitId, "Exit");

    SetForegroundWindow(window_handle);
    TrackPopupMenu(menu, TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                  cursor_position.x, cursor_position.y, 0, window_handle, NULL);
    DestroyMenu(menu);
}

static LRESULT CALLBACK WindowProc(HWND window_handle, UINT message,
                                   WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case kTrayIconMessage:
            if (lparam == WM_RBUTTONUP) {
                ShowTrayMenu(window_handle);
            }
            break;

        case WM_COMMAND:
            if (LOWORD(wparam) == kTrayExitId) {
                PostQuitMessage(0);
            } else if (LOWORD(wparam) == kTrayChangeHotkeyId) {
                ChangeHotkey(window_handle);
            }
            break;

        case WM_DESTROY:
            Shell_NotifyIcon(NIM_DELETE,
                &(NOTIFYICONDATA){
                    .cbSize = sizeof(NOTIFYICONDATA),
                    .hWnd = window_handle,
                    .uID = 1
                });
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(window_handle, message, wparam, lparam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE instance_handle, HINSTANCE prev_instance_handle,
                  LPSTR command_line, int show_command) {
    LoadConfiguration();

    WNDCLASS window_class = {0};
    window_class.lpfnWndProc = WindowProc;
    window_class.hInstance = instance_handle;
    window_class.lpszClassName = "KlipboardClass";
    RegisterClass(&window_class);

    HWND window_handle = CreateWindow("KlipboardClass", "Klipboard",
                                     0, 0, 0, 0, 0, NULL, NULL,
                                     instance_handle, NULL);
    g_main_window_handle = window_handle;

    // add tray
    NOTIFYICONDATA notify_icon_data = {0};
    notify_icon_data.cbSize = sizeof(NOTIFYICONDATA);
    notify_icon_data.hWnd = window_handle;
    notify_icon_data.uID = 1;
    notify_icon_data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    notify_icon_data.uCallbackMessage = kTrayIconMessage;
    notify_icon_data.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    snprintf(notify_icon_data.szTip,
         sizeof(notify_icon_data.szTip),
         "%s", "Klipboard");

    Shell_NotifyIcon(NIM_ADD, &notify_icon_data);


    g_keyboard_hook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc,
        NULL, 0);
    if (!g_keyboard_hook) {    // if hook fails, fallback to RegisterHotKey
        g_use_hotkey_fallback = 1;
        RegisterHotKey(window_handle, 1, 0, g_current_virtual_key);
    }
    ShowTrayBalloon("Klipboard", "Klipboard is running.");

    MSG message;
    while (GetMessage(&message, NULL, 0, 0)) {
        if (g_use_hotkey_fallback && message.message == WM_HOTKEY &&
            message.wParam == 1) {
            PasteClipboardText();
        }
        TranslateMessage(&message);
        DispatchMessage(&message);
    }

    if (g_use_hotkey_fallback) {
        UnregisterHotKey(window_handle, 1);
    } else {
        UnhookWindowsHookEx(g_keyboard_hook);
    }
    return 0;
}
