#include <Windows.h>
#include <Psapi.h>
#include <string>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>

// Structure to hold key rebinding information
struct KeyBinding {
    std::string processName;  // Name of the process/executable
    int originalKey;          // Original key code
    int targetKey;            // Target key code to simulate
};

// Global variables
std::vector<KeyBinding> keyBindings;
HHOOK keyboardHook = NULL;
bool isActive = true;
std::unordered_map<int, int> activeProcessBindings; // Cache for current process bindings
std::string currentProcessName; // Cache for current process name

// Initialize the key map at compile time
const std::unordered_map<std::string, int> KEY_MAP = {
    {"CAPSLOCK", VK_CAPITAL},
    {"CAPS_LOCK", VK_CAPITAL},
    {"CAPS", VK_CAPITAL},
    {"SHIFT", VK_SHIFT},
    {"CTRL", VK_CONTROL},
    {"CONTROL", VK_CONTROL},
    {"ALT", VK_MENU},
    {"TAB", VK_TAB},
    {"ENTER", VK_RETURN},
    {"RETURN", VK_RETURN},
    {"BACKSPACE", VK_BACK},
    {"ESC", VK_ESCAPE},
    {"ESCAPE", VK_ESCAPE},
    {"SPACE", VK_SPACE},
    {"SPACEBAR", VK_SPACE},
    {"DEL", VK_DELETE},
    {"DELETE", VK_DELETE},
    {"UP", VK_UP},
    {"DOWN", VK_DOWN},
    {"LEFT", VK_LEFT},
    {"RIGHT", VK_RIGHT},
    {"HOME", VK_HOME},
    {"END", VK_END},
    {"PGUP", VK_PRIOR},
    {"PAGEUP", VK_PRIOR},
    {"PGDN", VK_NEXT},
    {"PAGEDOWN", VK_NEXT},
    {"INS", VK_INSERT},
    {"INSERT", VK_INSERT},
    {"F1", VK_F1},
    {"F2", VK_F2},
    {"F3", VK_F3},
    {"F4", VK_F4},
    {"F5", VK_F5},
    {"F6", VK_F6},
    {"F7", VK_F7},
    {"F8", VK_F8},
    {"F9", VK_F9},
    {"F10", VK_F10},
    {"F11", VK_F11},
    {"F12", VK_F12}
};

// Update the cache of key bindings for the current process
void UpdateBindingsCache() {
    activeProcessBindings.clear();
    for (const auto& binding : keyBindings) {
        if (binding.processName == currentProcessName || binding.processName == "*") {
            activeProcessBindings[binding.originalKey] = binding.targetKey;
        }
    }
}

// Function to get active window executable name - optimized to cache result
std::string GetActiveProcessName() {
    static HWND lastWindow = NULL;
    static FILETIME lastWindowTime = { 0 };
    HWND foregroundWindow = GetForegroundWindow();

    if (!foregroundWindow) {
        return "";
    }

    // Check if window has changed
    FILETIME createTime, exitTime, kernelTime, userTime;
    DWORD processId;
    GetWindowThreadProcessId(foregroundWindow, &processId);

    HANDLE processHandle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
    if (!processHandle) {
        return "";
    }

    // Get process times to detect changes even with the same HWND
    if (GetProcessTimes(processHandle, &createTime, &exitTime, &kernelTime, &userTime)) {
        if (foregroundWindow == lastWindow &&
            createTime.dwLowDateTime == lastWindowTime.dwLowDateTime &&
            createTime.dwHighDateTime == lastWindowTime.dwHighDateTime) {
            CloseHandle(processHandle);
            return currentProcessName; // Return cached result
        }

        lastWindow = foregroundWindow;
        lastWindowTime = createTime;
    }

    char fileName[MAX_PATH];
    if (GetModuleFileNameExA(processHandle, NULL, fileName, MAX_PATH) == 0) {
        CloseHandle(processHandle);
        return "";
    }

    CloseHandle(processHandle);

    // Extract just the filename from the path - optimize by searching backward
    std::string fullPath(fileName);
    size_t lastSlash = fullPath.find_last_of("\\");

    currentProcessName = (lastSlash != std::string::npos) ?
        fullPath.substr(lastSlash + 1) : fullPath;

    // Update the bindings cache for this process
    UpdateBindingsCache();

    return currentProcessName;
}

// Function to check if a key is rebindable in the current context - optimized with cache
bool ShouldRebindKey(int keyCode, int& targetKey) {
    auto it = activeProcessBindings.find(keyCode);
    if (it != activeProcessBindings.end()) {
        targetKey = it->second;
        return true;
    }
    return false;
}

// Convert string key name to virtual key code - optimized with unordered_map
int StringToKeyCode(const std::string& keyName) {
    // Check if it's in our map
    auto it = KEY_MAP.find(keyName);
    if (it != KEY_MAP.end()) {
        return it->second;
    }

    // Single character (A-Z, 0-9)
    if (keyName.length() == 1) {
        char c = std::toupper(keyName[0]);

        // A-Z or 0-9
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            return c;
        }
    }

    // Could not recognize the key
    return 0;
}

// Function to load key bindings from config file - optimized for faster reading
bool LoadKeyBindings(const std::string& configFile) {
    std::ifstream file(configFile);
    if (!file.is_open()) {
        std::cerr << "Failed to open config file: " << configFile << std::endl;
        return false;
    }

    keyBindings.clear();
    keyBindings.reserve(50); // Reserve space for expected number of bindings
    std::string line;
    int lineNum = 0;

    while (std::getline(file, line)) {
        lineNum++;

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }

        std::istringstream iss(line);
        std::string processName, originalKeyStr, targetKeyStr;

        if (!(iss >> processName >> originalKeyStr >> targetKeyStr)) {
            std::cerr << "Error parsing line " << lineNum << ": " << line << std::endl;
            continue;
        }

        // Convert keys to uppercase inline
        std::transform(originalKeyStr.begin(), originalKeyStr.end(), originalKeyStr.begin(),
            [](unsigned char c) { return std::toupper(c); });
        std::transform(targetKeyStr.begin(), targetKeyStr.end(), targetKeyStr.begin(),
            [](unsigned char c) { return std::toupper(c); });

        // Convert key strings to key codes
        int originalKey = StringToKeyCode(originalKeyStr);
        int targetKey = StringToKeyCode(targetKeyStr);

        if (originalKey == 0) {
            std::cerr << "Unknown original key: " << originalKeyStr << " at line " << lineNum << std::endl;
            continue;
        }

        if (targetKey == 0) {
            std::cerr << "Unknown target key: " << targetKeyStr << " at line " << lineNum << std::endl;
            continue;
        }

        KeyBinding binding{ processName, originalKey, targetKey };
        keyBindings.push_back(binding);

        std::cout << "Loaded key binding: " << processName << " - "
            << originalKeyStr << " -> " << targetKeyStr << std::endl;
    }

    file.close();
    return !keyBindings.empty();
}

// Low-level keyboard hook procedure - optimized for performance
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && isActive) {
        KBDLLHOOKSTRUCT* kbStruct = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        int keyCode = kbStruct->vkCode;
        int targetKey;

        // Get process name only if we need to (window might have changed)
        static HWND lastCheckedWindow = NULL;
        HWND currentWindow = GetForegroundWindow();

        if (currentWindow != lastCheckedWindow) {
            GetActiveProcessName(); // This updates the cache
            lastCheckedWindow = currentWindow;
        }

        if (ShouldRebindKey(keyCode, targetKey)) {
            // Handle both key down and key up events
            bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
            bool isKeyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

            if (isKeyDown || isKeyUp) {
                INPUT input = {};
                input.type = INPUT_KEYBOARD;
                input.ki.wVk = targetKey;
                input.ki.dwFlags = isKeyUp ? KEYEVENTF_KEYUP : 0;
                SendInput(1, &input, sizeof(INPUT));

                // Block the original key
                return 1;
            }
        }
    }

    // Pass the key event to the next hook in the chain
    return CallNextHookEx(keyboardHook, nCode, wParam, lParam);
}

// Function to create a default config file if none exists
void CreateDefaultConfigFile(const std::string& configFile) {
    std::ofstream file(configFile);
    if (file.is_open()) {
        file << "# Key Rebinding Configuration File\n";
        file << "# Format: ProcessName OriginalKey TargetKey\n";
        file << "# Use '*' for ProcessName to apply to all processes\n";
        file << "# Example: FortniteClient-Win64-Shipping.exe CAPSLOCK Y\n\n";
        file << "FortniteClient-Win64-Shipping.exe CAPSLOCK Y\n";
        file << "# Add more bindings below:\n";
        file.close();
        std::cout << "Created default configuration file: " << configFile << std::endl;
    }
}

// Main function - optimized for cleaner exit handling
int main() {
    const std::string configFile = "keybindings.cfg";

    // Check if config file exists, create default if not
    {
        std::ifstream fileCheck(configFile);
        if (!fileCheck) {
            CreateDefaultConfigFile(configFile);
        }
    }

    // Load key bindings
    if (!LoadKeyBindings(configFile)) {
        std::cerr << "Failed to load key bindings. Please check the configuration file: " << configFile << std::endl;
        std::cerr << "Press any key to exit..." << std::endl;
        std::cin.get();
        return 1;
    }

    // Install the low-level keyboard hook
    keyboardHook = SetWindowsHookEx(
        WH_KEYBOARD_LL,
        LowLevelKeyboardProc,
        GetModuleHandle(NULL),
        0
    );

    if (!keyboardHook) {
        std::cerr << "Failed to set keyboard hook. Error code: " << GetLastError() << std::endl;
        std::cerr << "Press any key to exit..." << std::endl;
        std::cin.get();
        return 1;
    }

    std::cout << "Key rebinder is running. Press Ctrl+Alt+T to toggle or Ctrl+C to exit." << std::endl;

    // Add hotkey to toggle the rebinding on/off (Ctrl+Alt+T)
    if (!RegisterHotKey(NULL, 1, MOD_CONTROL | MOD_ALT, 'T')) {
        std::cerr << "Warning: Failed to register hotkey. Toggling will not be available." << std::endl;
    }

    // Message loop with error handling
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_HOTKEY && msg.wParam == 1) {
            isActive = !isActive;
            std::cout << "Key rebinding " << (isActive ? "enabled" : "disabled") << std::endl;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Clean up
    UnhookWindowsHookEx(keyboardHook);
    UnregisterHotKey(NULL, 1);
    return 0;
}