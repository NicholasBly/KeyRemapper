#include <Windows.h>
#include <Psapi.h>  // Added for GetModuleFileNameExA
#include <string>
#include <fstream>
#include <vector>
#include <map>
#include <iostream>
#include <sstream>
#include <algorithm>

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

// Function to get active window executable name
std::string GetActiveProcessName() {
    char fileName[MAX_PATH];
    HWND foregroundWindow = GetForegroundWindow();
    if (!foregroundWindow) {
        return "";
    }

    DWORD processId;
    GetWindowThreadProcessId(foregroundWindow, &processId);

    HANDLE processHandle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
    if (!processHandle) {
        return "";
    }

    if (GetModuleFileNameExA(processHandle, NULL, fileName, MAX_PATH) == 0) {
        CloseHandle(processHandle);
        return "";
    }

    CloseHandle(processHandle);

    // Extract just the filename from the path
    std::string fullPath(fileName);
    size_t lastSlash = fullPath.find_last_of("\\");
    if (lastSlash != std::string::npos) {
        return fullPath.substr(lastSlash + 1);
    }

    return fullPath;
}

// Function to check if a key is rebindable in the current context
bool ShouldRebindKey(int keyCode, std::string& currentProcess, int& targetKey) {
    for (const auto& binding : keyBindings) {
        if (binding.originalKey == keyCode &&
            (binding.processName == currentProcess || binding.processName == "*")) {
            targetKey = binding.targetKey;
            return true;
        }
    }
    return false;
}

// Convert string key name to virtual key code
int StringToKeyCode(const std::string& keyName) {
    // Common key mappings
    static std::map<std::string, int> keyMap = {
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

    // Check if it's in our map
    auto it = keyMap.find(keyName);
    if (it != keyMap.end()) {
        return it->second;
    }

    // Single character (A-Z, 0-9)
    if (keyName.length() == 1) {
        char c = toupper(keyName[0]);

        // A-Z
        if (c >= 'A' && c <= 'Z') {
            return c;
        }

        // 0-9
        if (c >= '0' && c <= '9') {
            return c;
        }
    }

    // Could not recognize the key
    return 0;
}

// Function to load key bindings from config file
bool LoadKeyBindings(const std::string& configFile) {
    std::ifstream file(configFile);
    if (!file.is_open()) {
        std::cerr << "Failed to open config file: " << configFile << std::endl;
        return false;
    }

    keyBindings.clear();
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

        // Convert keys to uppercase
        std::transform(originalKeyStr.begin(), originalKeyStr.end(), originalKeyStr.begin(), ::toupper);
        std::transform(targetKeyStr.begin(), targetKeyStr.end(), targetKeyStr.begin(), ::toupper);

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

        KeyBinding binding;
        binding.processName = processName;
        binding.originalKey = originalKey;
        binding.targetKey = targetKey;
        keyBindings.push_back(binding);

        std::cout << "Loaded key binding: " << processName << " - "
            << originalKeyStr << " -> " << targetKeyStr << std::endl;
    }

    file.close();
    return true;
}

// Low-level keyboard hook procedure
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && isActive) {
        KBDLLHOOKSTRUCT* kbStruct = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        int keyCode = kbStruct->vkCode;
        std::string currentProcess = GetActiveProcessName();
        int targetKey;

        if (ShouldRebindKey(keyCode, currentProcess, targetKey)) {
            // Handle both key down and key up events
            if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
                // For key down, only send key down event
                INPUT input = {};
                input.type = INPUT_KEYBOARD;
                input.ki.wVk = targetKey;
                input.ki.dwFlags = 0; // Key down
                SendInput(1, &input, sizeof(INPUT));

                // Block the original key
                return 1;
            }
            else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
                // For key up, only send key up event
                INPUT input = {};
                input.type = INPUT_KEYBOARD;
                input.ki.wVk = targetKey;
                input.ki.dwFlags = KEYEVENTF_KEYUP; // Key up
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

// Main function
int main() {
    const std::string configFile = "keybindings.cfg";

    // Check if config file exists, create default if not
    std::ifstream fileCheck(configFile);
    if (!fileCheck.good()) {
        fileCheck.close();
        CreateDefaultConfigFile(configFile);
    }
    else {
        fileCheck.close();
    }

    // Load key bindings
    if (!LoadKeyBindings(configFile)) {
        std::cerr << "Failed to load key bindings." << std::endl;
        std::cerr << "Please check the configuration file: " << configFile << std::endl;
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
        std::cerr << "Failed to set keyboard hook." << std::endl;
        std::cerr << "Press any key to exit..." << std::endl;
        std::cin.get();
        return 1;
    }

    std::cout << "Key rebinder is running. Press Ctrl+C to exit." << std::endl;

    // Add hotkey to toggle the rebinding on/off (Ctrl+Alt+T)
    RegisterHotKey(NULL, 1, MOD_CONTROL | MOD_ALT, 'T');

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_HOTKEY && msg.wParam == 1) {
            isActive = !isActive;
            std::cout << "Key rebinding " << (isActive ? "enabled" : "disabled") << std::endl;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Clean up
    UnhookWindowsHookEx(keyboardHook);
    return 0;
}