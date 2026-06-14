#include <windows.h>
#include <sddl.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace {

constexpr ULONGLONG kCatslockDoubleTapMs = 500;
constexpr wchar_t kCatslockToggleEventName[] = L"CatslockToggle";
constexpr wchar_t kCatslockInstanceMutexName[] = L"CatslockInstance";

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

    UnhookWindowsHookEx(g_keyboardHook);
    CloseHandle(g_toggleEvent);
    ReleaseMutex(g_instanceMutex);
    CloseHandle(g_instanceMutex);
    LogCatslock("shutdown");
    return 0;
}
