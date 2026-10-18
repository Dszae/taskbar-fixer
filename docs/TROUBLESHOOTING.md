# Troubleshooting & FAQ

This guide addresses common issues, diagnostic steps, and tips for configuring the **Windows 11 App Specific Taskbar Hider** mod.

---

## 1. How to Find the Exact Process Name (.exe)

The target process must match the filename of the application's executable (case-insensitive, without paths).

### Method A: Using PowerShell (Fastest)
Bring your target application to the front, open PowerShell, and run:
```powershell
Get-Process | Where-Object { $_.MainWindowHandle -eq (Get-Process -Id (Get-Process | Where-Object { $_.MainWindowTitle }).Id).MainWindowHandle } | Select-Object -ExpandProperty ProcessName
```
Or simply list all running applications with visible windows:
```powershell
Get-Process | Where-Object { $_.MainWindowTitle -ne "" } | Select-Object ProcessName, Id, MainWindowTitle | Format-Table -AutoSize
```
Add `.exe` to the resulting `ProcessName` (e.g., `Resolve` becomes `Resolve.exe`).

### Method B: Using Windows Task Manager
1. Press `Ctrl + Shift + Esc` to open Task Manager.
2. Click on the **Details** tab on the left sidebar.
3. Find your application in the list. The value in the **Name** column is your target process (e.g., `FL64.exe`, `Photoshop.exe`, `Code.exe`).

---

## 2. Common Application Executable Names

| Application | Process Name | Notes |
| :--- | :--- | :--- |
| **DaVinci Resolve** | `Resolve.exe` | Works for both Free and Studio editions |
| **Adobe Photoshop** | `Photoshop.exe` | Creative Cloud editions |
| **Adobe Premiere Pro**| `Premiere Pro.exe` | |
| **Adobe InDesign** | `InDesign.exe` | |
| **FL Studio** | `FL64.exe` (or `FL.exe`) | Usually 64-bit (`FL64.exe`) |
| **Ableton Live** | `Ableton Live 11 Suite.exe` / `Ableton Live 12 Suite.exe` | Check exact version in Task Manager Details |
| **Blender** | `blender.exe` | |
| **Visual Studio Code**| `Code.exe` | |
| **Windows Terminal** | `WindowsTerminal.exe` | |
| **Autodesk Maya** | `maya.exe` | |

---

## 3. The Taskbar Is Still Popping Up On Hover

If the taskbar still appears when your mouse hits the bottom edge:

1. **Verify Windows Default Auto-Hide is OFF**:
   - Go to Windows **Settings** > **Personalization** > **Taskbar** > **Taskbar behaviors**.
   - Make sure **"Automatically hide the taskbar" is UNCHECKED**.
   - *Why?* Windows' native auto-hide implementation uses additional animation timers that can fight with custom hooks. This mod manages hiding and unhiding completely on its own.
2. **Check "Require focus" Setting**:
   - If `Require focus` is enabled (`true`), the taskbar will reappear as soon as another window (or a floating palette owned by another process) gains focus.
   - If your workflow involves floating windows across multiple processes, set `Require focus` to `false` (hides as long as the process is running).
3. **Verify the Process Name**:
   - Some launchers launch child processes under a different executable name. Use Task Manager's **Details** tab to verify the exact process handling the main GUI.

---

## 4. Viewing Windhawk Mod Logs

Windhawk includes an integrated logger that captures all `Wh_Log` events:

1. Open the Windhawk application.
2. Navigate to the mod's details page.
3. Switch to the **Logs** tab.
4. You will see real-time outputs such as:
   ```text
   Initializing Windows 11 App Specific Taskbar Hider
   Condition changed, hiding taskbar
   Condition changed, showing taskbar
   ```
If you see no log outputs, verify that the mod is compiled and enabled.

---

## 5. How to Revert or Uninstall

1. Open **Windhawk**.
2. Find **Windows 11 App Specific Taskbar Hider** in your installed mods list.
3. Toggle the switch to **Disabled** or click **Uninstall**.
4. The mod will automatically restore your taskbar (`ShowWindow(SW_SHOW)`) and reset the desktop work area to default. No reboot or explorer restart is required!

