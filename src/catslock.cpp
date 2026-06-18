#include <windows.h>
#include <shellapi.h>
#include <sddl.h>

#include <atomic>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace {

constexpr ULONGLONG kCatslockDoubleTapMs = 500;
constexpr wchar_t kCatslockToggleEventName[] = L"CatslockToggle";
constexpr wchar_t kCatslockInstanceMutexName[] = L"CatslockInstance";
constexpr wchar_t kCatslockTrayWindowClass[] = L"CatslockTrayWindow";
constexpr UINT kCatslockTrayIconId = 1;
constexpr UINT kCatslockTrayMessage = WM_APP + 1;
constexpr UINT kCatslockTrayUpdateMessage = WM_APP + 2;
constexpr UINT_PTR kCatslockMenuToggle = 1001;
constexpr UINT_PTR kCatslockMenuExit = 1002;

enum class CatslockState {
    Idle,
    FirstTap,
    CatslockOn,
    FirstTapOff,
    CatslockOff,
};

HHOOK g_keyboardHook = nullptr;
HANDLE g_toggleEvent = nullptr;
HANDLE g_instanceMutex = nullptr;
HWND g_trayWindow = nullptr;
HICON g_trayIcon = nullptr;
std::atomic<CatslockState> g_state{CatslockState::Idle};
std::atomic<ULONGLONG> g_lastCapsLockDownTick{0};
std::atomic<int> g_capsLockCorrectionEvents{0};

std::filesystem::path CatslockStatePath() {
    wchar_t tempPath[MAX_PATH] = {};
    DWORD length = GetTempPathW(MAX_PATH, tempPath);
    if (length == 0 || length >= MAX_PATH) {
        return std::filesystem::path(L"catslock.state");
    }

    return std::filesystem::path(tempPath) / L"catslock.state";
}

std::filesystem::path CatslockLogPath() {
    wchar_t tempPath[MAX_PATH] = {};
    DWORD length = GetTempPathW(MAX_PATH, tempPath);
    if (length == 0 || length >= MAX_PATH) {
        return std::filesystem::path(L"catslock.log");
    }

    return std::filesystem::path(tempPath) / L"catslock.log";
}

void LogCatslock(const std::string& message) {
    std::ofstream logFile(CatslockLogPath(), std::ios::app);
    if (!logFile) {
        return;
    }

    logFile << GetTickCount64() << " " << message << '\n';
}

bool IsCatslockOnState(CatslockState state) {
    return state == CatslockState::CatslockOn || state == CatslockState::FirstTapOff;
}

const char* CatslockStateName(CatslockState state) {
    switch (state) {
    case CatslockState::Idle:
        return "IDLE";
    case CatslockState::FirstTap:
        return "FIRST_TAP";
    case CatslockState::CatslockOn:
        return "CATSLOCK_ON";
    case CatslockState::FirstTapOff:
        return "FIRST_TAP_OFF";
    case CatslockState::CatslockOff:
        return "CATSLOCK_OFF";
    }

    return "IDLE";
}

void WriteCatslockState(CatslockState state) {
    std::ofstream stateFile(CatslockStatePath(), std::ios::trunc);
    if (!stateFile) {
        return;
    }

    stateFile << CatslockStateName(state) << '\n';
}

bool ReadCatslockRequestedState(bool* enabled) {
    std::ifstream stateFile(CatslockStatePath());
    if (!stateFile) {
        return false;
    }

    std::string state;
    std::getline(stateFile, state);

    if (state == "CATSLOCK_ON" || state == "FIRST_TAP_OFF" || state == "ON") {
        *enabled = true;
        return true;
    }

    if (state == "CATSLOCK_OFF" || state == "IDLE" || state == "FIRST_TAP" || state == "OFF") {
        *enabled = false;
        return true;
    }

    return false;
}

void TransitionCatslock(CatslockState nextState) {
    g_state.store(nextState, std::memory_order_release);
    WriteCatslockState(nextState);
    LogCatslock(std::string("state=") + CatslockStateName(nextState));
    if (g_trayWindow != nullptr) {
        PostMessageW(g_trayWindow, kCatslockTrayUpdateMessage, 0, 0);
    }
}

void CorrectCapsLockToggleState() {
    if ((GetKeyState(VK_CAPITAL) & 0x0001) == 0) {
        return;
    }

    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CAPITAL;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = VK_CAPITAL;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

    g_capsLockCorrectionEvents.store(2, std::memory_order_release);
    if (SendInput(2, inputs, sizeof(INPUT)) != 2) {
        g_capsLockCorrectionEvents.store(0, std::memory_order_release);
        LogCatslock("capslock-correction=failed");
    } else {
        LogCatslock("capslock-correction=sent");
    }
}

void SetCatslockEnabled(bool enabled) {
    g_lastCapsLockDownTick.store(0, std::memory_order_release);
    TransitionCatslock(enabled ? CatslockState::CatslockOn : CatslockState::CatslockOff);
    if (enabled) {
        CorrectCapsLockToggleState();
    }
}

void ToggleCatslock() {
    SetCatslockEnabled(!IsCatslockOnState(g_state.load(std::memory_order_acquire)));
}

void HandleCapsLockDown() {
    ULONGLONG now = GetTickCount64();
    ULONGLONG lastTap = g_lastCapsLockDownTick.load(std::memory_order_acquire);
    CatslockState state = g_state.load(std::memory_order_acquire);
    bool isDoubleTap = lastTap != 0 && now - lastTap < kCatslockDoubleTapMs;

    if (IsCatslockOnState(state)) {
        if (isDoubleTap) {
            SetCatslockEnabled(false);
        } else {
            g_lastCapsLockDownTick.store(now, std::memory_order_release);
            TransitionCatslock(CatslockState::FirstTapOff);
        }
        return;
    }

    if (isDoubleTap) {
        SetCatslockEnabled(true);
        return;
    }

    g_lastCapsLockDownTick.store(now, std::memory_order_release);
    TransitionCatslock(CatslockState::FirstTap);
}

LRESULT CALLBACK CatslockKeyboardProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION) {
        auto* keyInfo = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        bool keyDown = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
        bool capsLockCorrection = keyInfo->vkCode == VK_CAPITAL &&
            (keyInfo->flags & LLKHF_INJECTED) != 0 &&
            g_capsLockCorrectionEvents.load(std::memory_order_acquire) > 0;
        bool wasCatslockOn = IsCatslockOnState(g_state.load(std::memory_order_acquire));

        if (capsLockCorrection) {
            g_capsLockCorrectionEvents.fetch_sub(1, std::memory_order_acq_rel);
            return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
        }

        if (keyDown && keyInfo->vkCode == VK_CAPITAL) {
            HandleCapsLockDown();
        }

        if (wasCatslockOn || IsCatslockOnState(g_state.load(std::memory_order_acquire))) {
            // Catslock silently discards all keyboard input while active, including
            // CapsLock events so the LED state does not change during activation.
            return 1;
        }
    }

    return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
}

void WatchCatslockToggleEvent() {
    while (true) {
        DWORD waitResult = WaitForSingleObject(g_toggleEvent, INFINITE);
        if (waitResult != WAIT_OBJECT_0) {
            LogCatslock("toggle-event-wait=failed");
            continue;
        }

        LogCatslock("toggle-event=signaled");
        bool enabled = false;
        if (ReadCatslockRequestedState(&enabled)) {
            SetCatslockEnabled(enabled);
        } else {
            ToggleCatslock();
        }
    }
}

HANDLE CreateCatslockToggleEvent() {
    PSECURITY_DESCRIPTOR securityDescriptor = nullptr;
    SECURITY_ATTRIBUTES securityAttributes = {};
    securityAttributes.nLength = sizeof(securityAttributes);

    // Catslock normally runs elevated. This low-integrity label plus broad DACL
    // lets non-admin terminals open and signal CatslockToggle.
    constexpr wchar_t kCatslockEventSecurity[] =
        L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;IU)(A;;GA;;;WD)S:(ML;;NW;;;LW)";

    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
            kCatslockEventSecurity,
            SDDL_REVISION_1,
            &securityDescriptor,
            nullptr)) {
        securityAttributes.lpSecurityDescriptor = securityDescriptor;
    }

    HANDLE eventHandle = CreateEventW(
        securityDescriptor == nullptr ? nullptr : &securityAttributes,
        FALSE,
        FALSE,
        kCatslockToggleEventName);

    if (securityDescriptor != nullptr) {
        LocalFree(securityDescriptor);
    }

    return eventHandle;
}

void FillTrayIconData(NOTIFYICONDATAW* iconData) {
    *iconData = {};
    iconData->cbSize = sizeof(*iconData);
    iconData->hWnd = g_trayWindow;
    iconData->uID = kCatslockTrayIconId;
}

void SetTrayTooltip(NOTIFYICONDATAW* iconData) {
    CatslockState state = g_state.load(std::memory_order_acquire);
    const wchar_t* tooltip = IsCatslockOnState(state) ? L"Catslock: ON" : L"Catslock: OFF";
    wcscpy_s(iconData->szTip, tooltip);
}

void UpdateTrayIcon() {
    if (g_trayWindow == nullptr) {
        return;
    }

    NOTIFYICONDATAW iconData = {};
    FillTrayIconData(&iconData);
    iconData.uFlags = NIF_TIP;
    SetTrayTooltip(&iconData);
    Shell_NotifyIconW(NIM_MODIFY, &iconData);
}

bool AddTrayIcon() {
    if (g_trayWindow == nullptr) {
        return false;
    }

    g_trayIcon = LoadIconW(nullptr, IDI_SHIELD);
    if (g_trayIcon == nullptr) {
        g_trayIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }

    NOTIFYICONDATAW iconData = {};
    FillTrayIconData(&iconData);
    iconData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    iconData.uCallbackMessage = kCatslockTrayMessage;
    iconData.hIcon = g_trayIcon;
    SetTrayTooltip(&iconData);

    if (!Shell_NotifyIconW(NIM_ADD, &iconData)) {
        return false;
    }

    iconData.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &iconData);
    return true;
}

void RemoveTrayIcon() {
    if (g_trayWindow == nullptr) {
        return;
    }

    NOTIFYICONDATAW iconData = {};
    FillTrayIconData(&iconData);
    Shell_NotifyIconW(NIM_DELETE, &iconData);
}

void ShowTrayMenu(HWND window) {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }

    CatslockState state = g_state.load(std::memory_order_acquire);
    AppendMenuW(
        menu,
        MF_STRING,
        kCatslockMenuToggle,
        IsCatslockOnState(state) ? L"Turn Catslock Off" : L"Turn Catslock On");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCatslockMenuExit, L"Exit");

    POINT cursor = {};
    GetCursorPos(&cursor);
    SetForegroundWindow(window);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, window, nullptr);
    DestroyMenu(menu);
}

LRESULT CALLBACK CatslockTrayWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case kCatslockTrayMessage:
        if (LOWORD(lParam) == WM_LBUTTONDBLCLK) {
            ToggleCatslock();
            return 0;
        }

        if (LOWORD(lParam) == WM_CONTEXTMENU || LOWORD(lParam) == WM_RBUTTONUP) {
            ShowTrayMenu(window);
            return 0;
        }
        break;

    case kCatslockTrayUpdateMessage:
        UpdateTrayIcon();
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kCatslockMenuToggle:
            ToggleCatslock();
            return 0;
        case kCatslockMenuExit:
            PostQuitMessage(0);
            return 0;
        }
        break;

    case WM_DESTROY:
        RemoveTrayIcon();
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

HWND CreateTrayWindow(HINSTANCE instance) {
    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = CatslockTrayWindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kCatslockTrayWindowClass;

    if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return nullptr;
    }

    return CreateWindowExW(
        0,
        kCatslockTrayWindowClass,
        L"Catslock",
        WS_OVERLAPPED,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        0,
        0,
        nullptr,
        nullptr,
        instance,
        nullptr);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    LogCatslock("startup");

    g_instanceMutex = CreateMutexW(nullptr, TRUE, kCatslockInstanceMutexName);
    if (g_instanceMutex == nullptr) {
        MessageBoxW(nullptr, L"Failed to create CatslockInstance mutex.", L"Catslock", MB_ICONERROR);
        LogCatslock("instance-mutex=create-failed");
        return 1;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        LogCatslock("instance-mutex=already-running");
        CloseHandle(g_instanceMutex);
        return 0;
    }

    WriteCatslockState(CatslockState::Idle);

    g_trayWindow = CreateTrayWindow(instance);
    if (g_trayWindow == nullptr) {
        LogCatslock("tray-window=create-failed");
    } else if (AddTrayIcon()) {
        LogCatslock("tray-icon=installed");
    } else {
        LogCatslock("tray-icon=install-failed");
    }

    g_toggleEvent = CreateCatslockToggleEvent();
    if (g_toggleEvent == nullptr) {
        MessageBoxW(nullptr, L"Failed to create CatslockToggle event.", L"Catslock", MB_ICONERROR);
        LogCatslock("toggle-event=create-failed");
        CloseHandle(g_instanceMutex);
        return 1;
    }
    LogCatslock("toggle-event=created");

    std::thread(WatchCatslockToggleEvent).detach();

    g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, CatslockKeyboardProc, instance, 0);
    if (g_keyboardHook == nullptr) {
        MessageBoxW(nullptr, L"Failed to install Catslock keyboard hook.", L"Catslock", MB_ICONERROR);
        LogCatslock("keyboard-hook=install-failed");
        CloseHandle(g_toggleEvent);
        CloseHandle(g_instanceMutex);
        return 1;
    }
    LogCatslock("keyboard-hook=installed");

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    RemoveTrayIcon();
    if (g_trayWindow != nullptr) {
        DestroyWindow(g_trayWindow);
    }
    UnhookWindowsHookEx(g_keyboardHook);
    CloseHandle(g_toggleEvent);
    ReleaseMutex(g_instanceMutex);
    CloseHandle(g_instanceMutex);
    LogCatslock("shutdown");
    return 0;
}
