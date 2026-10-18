// ==WindhawkMod==
// @id              windows11-app-specific-taskbar-hider
// @name            Windows 11 App Specific Taskbar Hider
// @description     Hides the Windows 11 taskbar and blocks accidental hover/edge reveal while selected apps are running
// @version         1.2
// @author          KWWDCoding (Maintained by Dszae)
// @github          https://github.com/Dszae/taskbar-fixer
// @homepage        https://github.com/Dszae/taskbar-fixer
// @include         explorer.exe
// @architecture    x86-64
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Windows 11 App Specific Taskbar Hider

Hides the Windows 11 taskbar and Utilises the full screen while one of your
chosen applications is running (optionally: only while it's focused). Unlike
just hiding the window, this version also blocks Windows own hover/edge
auto-reveal, so accidentally moving the mouse into the taskbar's screen area
(e.g. a timeline scrollbar right at the bottom edge in DaVinci Resolve, or a
horizontal scrollbar in Photoshop/InDesign) will NOT bring the taskbar back
or steal focus from your app.

## How it works

Windows shell has its own independent logic that watches the cursor and
slides the taskbar back in whenever it nears the screen edge - this happens
regardless of what any external tool did to the window a moment earlier,
which is why simply hiding it on a timer still let it "pop up" on hover.

This mod instead hooks the Windows APIs the shell uses to show/activate the
taskbar (`ShowWindow`, `SetWindowPos`, `SetForegroundWindow`) and, while your
condition is active, silently blocks any attempt to show or focus the
taskbar. The window is simply never allowed to appear, so hovering into its
area does nothing.

## Usage

1. In the mod's settings, list the EXE filenames (just the filename, not the
   full path) of the apps that should trigger hiding, e.g. `Resolve.exe`,
   `Photoshop.exe`, `FL64.exe`, `InDesign.exe`.
2. Choose whether hiding should require the app to be focused, or just
   running (see the "Require focus" setting).
3. Enable the mod. You do NOT need to enable Windows' own "Automatically
   hide the taskbar" setting for this - this mod takes full control instead
   and it's recommended to leave Windows' own auto-hide OFF to avoid the two
   fighting each other.
4. Even with the taskbar hidden, you can still access the Start menu by
   pressing the Win key (this is deliberately not blocked).

Note: the mod polls the running process / focus state a few times a second,
so there is a short delay (well under a second) between switching apps and
the hide state updating. The hover-blocking itself is instant since it
intercepts the show attempt directly, not on a timer.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- hideTaskbar: true
  $name: Enable taskbar auto-hiding
  $description: >-
    Master switch. When enabled, the taskbar will be hidden and its
    accidental hover/edge reveal blocked, based on the target processes
    below (see "Require focus").
- targetProcesses: [Resolve.exe, Photoshop.exe, FL64.exe, InDesign.exe]
  $name: Target processes
  $description: >-
    EXE filenames (no path) that should trigger taskbar hiding, e.g.
    Resolve.exe, Photoshop.exe, FL64.exe, InDesign.exe. Add or remove entries
    as needed.
- requireFocus: true
  $name: Require focus (not just running)
  $description: >-
    If enabled, hiding is only active while one of the target apps is the
    active/foreground window - switching to another app brings the taskbar
    back. If disabled, the taskbar stays hidden as long as the target app is
    running at all, regardless of focus.
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <tlhelp32.h>
#include <windhawk_utils.h>

#include <string>
#include <vector>
#include <atomic>

struct {
    bool hideTaskbar;
    bool requireFocus;
    std::vector<std::wstring> targetProcesses;
} settings;

// Guards `settings` between the settings-load calls (main thread) and the
// polling thread / hooks reading it.
CRITICAL_SECTION g_settingsLock;

HANDLE g_pollThread = nullptr;
std::atomic<bool> g_stopPolling{false};

// The single source of truth the hooks check: while true, any attempt to
// show/activate/reposition-into-view the taskbar is blocked.
std::atomic<bool> g_forceHidden{false};

HWND g_taskbarHwnd = nullptr;

// ---------------------------------------------------------------------
// Taskbar visibility helpers
// ---------------------------------------------------------------------

HWND FindTaskbarWindow() {
    return FindWindow(L"Shell_TrayWnd", NULL);
}

void AdjustWorkArea(bool useFullScreen) {
    MONITORINFO mi = { sizeof(mi) };
    HMONITOR hMonitor = MonitorFromWindow(NULL, MONITOR_DEFAULTTOPRIMARY);

    if (GetMonitorInfo(hMonitor, &mi)) {
        RECT rcWork = mi.rcMonitor;

        if (!useFullScreen) {
            SystemParametersInfo(SPI_GETWORKAREA, 0, &rcWork, 0);
        } else {
            rcWork = mi.rcMonitor;
            SystemParametersInfo(SPI_SETWORKAREA, 0, &rcWork, SPIF_SENDCHANGE | SPIF_UPDATEINIFILE);
        }
    }
}

// Actually moves the window and updates the work area. Safe to call this
// with real ShowWindow/SetWindowPos since our hooks only block *show*
// attempts while g_forceHidden is true - hiding always passes through.
void ApplyTaskbarVisibility(bool hide) {
    HWND hTaskbar = g_taskbarHwnd ? g_taskbarHwnd : FindTaskbarWindow();
    if (hTaskbar) {
        if (hide) {
            ShowWindow(hTaskbar, SW_HIDE);
            AdjustWorkArea(true);
        } else {
            ShowWindow(hTaskbar, SW_SHOW);
            AdjustWorkArea(false);
        }
    }
}

bool ExeNameMatches(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

std::wstring GetForegroundProcessExeName() {
    HWND hWnd = GetForegroundWindow();
    if (!hWnd) {
        return L"";
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hWnd, &pid);
    if (!pid) {
        return L"";
    }

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) {
        return L"";
    }

    WCHAR buffer[MAX_PATH];
    DWORD size = MAX_PATH;
    std::wstring result;

    if (QueryFullProcessImageNameW(hProcess, 0, buffer, &size)) {
        std::wstring fullPath(buffer);
        size_t pos = fullPath.find_last_of(L"\\/");
        result = (pos == std::wstring::npos) ? fullPath : fullPath.substr(pos + 1);
    }

    CloseHandle(hProcess);
    return result;
}

bool IsAnyTargetProcessFocused() {
    std::vector<std::wstring> targets;
    {
        EnterCriticalSection(&g_settingsLock);
        targets = settings.targetProcesses;
        LeaveCriticalSection(&g_settingsLock);
    }
    if (targets.empty()) return false;

    std::wstring foregroundExe = GetForegroundProcessExeName();
    if (foregroundExe.empty()) return false;

    for (const auto& target : targets) {
        if (ExeNameMatches(foregroundExe, target)) return true;
    }
    return false;
}

bool IsAnyTargetProcessRunning() {
    std::vector<std::wstring> targets;
    {
        EnterCriticalSection(&g_settingsLock);
        targets = settings.targetProcesses;
        LeaveCriticalSection(&g_settingsLock);
    }
    if (targets.empty()) return false;

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return false;

    bool found = false;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(hSnapshot, &pe)) {
        do {
            for (const auto& target : targets) {
                if (ExeNameMatches(pe.szExeFile, target)) {
                    found = true;
                    break;
                }
            }
            if (found) break;
        } while (Process32NextW(hSnapshot, &pe));
    }

    CloseHandle(hSnapshot);
    return found;
}

// Recomputes whether hiding should be active and applies it. This drives
// both the "hide it now" action and the g_forceHidden flag the hooks below
// check on every show/activate attempt.
void UpdateTaskbarState() {
    bool masterEnabled;
    bool requireFocus;
    {
        EnterCriticalSection(&g_settingsLock);
        masterEnabled = settings.hideTaskbar;
        requireFocus = settings.requireFocus;
        LeaveCriticalSection(&g_settingsLock);
    }

    bool conditionMet = requireFocus ? IsAnyTargetProcessFocused()
                                      : IsAnyTargetProcessRunning();
    bool shouldHide = masterEnabled && conditionMet;

    bool wasHidden = g_forceHidden.exchange(shouldHide);
    if (shouldHide != wasHidden) {
        Wh_Log(L"Condition changed, %s taskbar", shouldHide ? L"hiding" : L"showing");
        ApplyTaskbarVisibility(shouldHide);
    }
}

// ---------------------------------------------------------------------
// API hooks: block anything that would show/activate the taskbar while
// g_forceHidden is set, so hovering into its screen area can't reveal it.
// ---------------------------------------------------------------------

using ShowWindow_t = BOOL(WINAPI*)(HWND, int);
ShowWindow_t g_origShowWindow;

BOOL WINAPI Hook_ShowWindow(HWND hWnd, int nCmdShow) {
    if (g_forceHidden && hWnd == g_taskbarHwnd) {
        switch (nCmdShow) {
            case SW_SHOW:
            case SW_SHOWNA:
            case SW_SHOWNORMAL:
            case SW_SHOWNOACTIVATE:
            case SW_RESTORE:
                return TRUE; // pretend it worked, do nothing
        }
    }
    return g_origShowWindow(hWnd, nCmdShow);
}

using SetWindowPos_t = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
SetWindowPos_t g_origSetWindowPos;

BOOL WINAPI Hook_SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y,
                               int cx, int cy, UINT uFlags) {
    if (g_forceHidden && hWnd == g_taskbarHwnd) {
        // Strip any show/activate intent, force it to stay hidden.
        uFlags &= ~SWP_SHOWWINDOW;
        uFlags |= SWP_NOACTIVATE | SWP_HIDEWINDOW;
    }
    return g_origSetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

using SetForegroundWindow_t = BOOL(WINAPI*)(HWND);
SetForegroundWindow_t g_origSetForegroundWindow;

BOOL WINAPI Hook_SetForegroundWindow(HWND hWnd) {
    if (g_forceHidden && hWnd == g_taskbarHwnd) {
        return FALSE;
    }
    return g_origSetForegroundWindow(hWnd);
}

void InstallHooks() {
    HMODULE hUser32 = GetModuleHandle(L"user32.dll");
    if (!hUser32) return;

    void* pShowWindow = (void*)GetProcAddress(hUser32, "ShowWindow");
    void* pSetWindowPos = (void*)GetProcAddress(hUser32, "SetWindowPos");
    void* pSetForegroundWindow = (void*)GetProcAddress(hUser32, "SetForegroundWindow");

    if (pShowWindow) {
        Wh_SetFunctionHook(pShowWindow, (void*)Hook_ShowWindow, (void**)&g_origShowWindow);
    }
    if (pSetWindowPos) {
        Wh_SetFunctionHook(pSetWindowPos, (void*)Hook_SetWindowPos, (void**)&g_origSetWindowPos);
    }
    if (pSetForegroundWindow) {
        Wh_SetFunctionHook(pSetForegroundWindow, (void*)Hook_SetForegroundWindow, (void**)&g_origSetForegroundWindow);
    }
}

// ---------------------------------------------------------------------
// Polling thread: keeps the condition (running/focused target app) up to
// date. The actual hover-blocking is instant via the hooks above; this
// just decides *when* g_forceHidden should be true or false.
// ---------------------------------------------------------------------

DWORD WINAPI PollThreadProc(LPVOID) {
    while (!g_stopPolling) {
        UpdateTaskbarState();
        for (int i = 0; i < 3 && !g_stopPolling; i++) {
            Sleep(100);
        }
    }
    return 0;
}

void StartPollThread() {
    g_stopPolling = false;
    g_pollThread = CreateThread(nullptr, 0, PollThreadProc, nullptr, 0, nullptr);
}

void StopPollThread() {
    if (g_pollThread) {
        g_stopPolling = true;
        WaitForSingleObject(g_pollThread, 5000);
        CloseHandle(g_pollThread);
        g_pollThread = nullptr;
    }
}

void LoadSettings() {
    EnterCriticalSection(&g_settingsLock);

    settings.hideTaskbar = Wh_GetIntSetting(L"hideTaskbar");
    settings.requireFocus = Wh_GetIntSetting(L"requireFocus");

    settings.targetProcesses.clear();
    for (int i = 0;; i++) {
        PCWSTR value = Wh_GetStringSetting(L"targetProcesses[%d]", i);
        bool stop = !value || !*value;
        if (!stop) {
            settings.targetProcesses.push_back(value);
        }
        Wh_FreeStringSetting(value);
        if (stop) break;
    }

    LeaveCriticalSection(&g_settingsLock);
}

BOOL Wh_ModInit() {
    Wh_Log(L"Initializing Windows 11 App Specific Taskbar Hider");

    InitializeCriticalSection(&g_settingsLock);
    g_taskbarHwnd = FindTaskbarWindow();

    LoadSettings();
    InstallHooks();

    g_forceHidden = false;
    UpdateTaskbarState();

    StartPollThread();

    return TRUE;
}

void Wh_ModUninit() {
    Wh_Log(L"Uninitializing Windows 11 App Specific Taskbar Hider");

    StopPollThread();

    g_forceHidden = false; // let a real ShowWindow(SW_SHOW) through
    ApplyTaskbarVisibility(false);

    DeleteCriticalSection(&g_settingsLock);
}

void Wh_ModSettingsChanged() {
    Wh_Log(L"Settings changed");

    LoadSettings();
    UpdateTaskbarState();
}

