# Contributing to Taskbar Fixer

Thank you for your interest in improving **Taskbar Fixer** (`windows11-app-specific-taskbar-hider`)! We welcome contributions, bug reports, and enhancements from the community.

---

## Development Workflow with Windhawk

Windhawk makes developing and testing mods straightforward without needing a heavy local Visual Studio installation.

1. **Install Windhawk**: Download and install Windhawk from [windhawk.net](https://windhawk.net/).
2. **Open the Mod Editor**:
   - In Windhawk, go to the top-right menu (three dots `...`) > **Advanced** > **Create Local Mod**.
   - Paste the contents of [`windows11-app-specific-taskbar-hider.wh.cpp`](../windows11-app-specific-taskbar-hider.wh.cpp).
3. **Compile & Test**:
   - Click **Compile Mod**. Windhawk compiles the C++ code using its bundled toolchain and injects it into `explorer.exe`.
   - Check the **Logs** tab in Windhawk to verify hook initialization and condition updates.
4. **Make Changes**:
   - Edit the source code directly or in your preferred editor (VS Code, CLion, etc.).
   - Re-compile inside Windhawk to hot-reload the mod into `explorer.exe`.

---

## Guidelines

- **Preserve Stability**: The mod runs inside `explorer.exe` (the core Windows desktop shell). Crashes inside `explorer.exe` will cause the taskbar/desktop to restart. Always safeguard pointer dereferences, validate window handles, and avoid blocking operations in hooks.
- **Thread Safety**: Any access to settings or global structures shared across the polling thread and hook functions must be synchronized with `g_settingsLock` or use atomic operations.
- **Code Style**:
  - Keep C++ formatting clean and consistent with existing code.
  - Document any non-trivial Win32 API interactions.
  - Test on both single and multi-monitor setups when modifying window or work area calculations.

---

## Submitting Pull Requests

1. Fork the repository.
2. Create a feature branch: `git checkout -b feature/my-enhancement`.
3. Test your modifications thoroughly in Windhawk.
4. Commit your changes: `git commit -m "feat: enhance work area calculation on secondary monitors"`.
5. Push to your branch and open a Pull Request.

