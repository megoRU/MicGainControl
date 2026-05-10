#include "ConfigManager.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace {
int ParseMicrophoneVolumePercent(const std::string& jsonText) {
    std::size_t keyPosition = jsonText.find("\"microphoneVolume\"");
    if (keyPosition == std::string::npos) {
        return 100;
    }

    std::size_t colonPosition = jsonText.find(':', keyPosition);
    if (colonPosition == std::string::npos) {
        return 100;
    }

    std::size_t numberStart = jsonText.find_first_of("-0123456789", colonPosition + 1);
    if (numberStart == std::string::npos) {
        return 100;
    }

    std::size_t numberEnd = jsonText.find_first_not_of("0123456789", numberStart + 1);
    std::string numberToken = jsonText.substr(numberStart, numberEnd - numberStart);

    try {
        return std::stoi(numberToken);
    } catch (...) {
        return 100;
    }
}

bool ParseEnabled(const std::string& jsonText) {
    std::size_t keyPosition = jsonText.find("\"enabled\"");
    if (keyPosition == std::string::npos) {
        return true;
    }

    std::size_t colonPosition = jsonText.find(':', keyPosition);
    if (colonPosition == std::string::npos) {
        return true;
    }

    std::size_t valueStart = jsonText.find_first_not_of(" \t\r\n", colonPosition + 1);
    if (valueStart == std::string::npos) {
        return true;
    }

    if (jsonText.compare(valueStart, 4, "true") == 0) {
        return true;
    }

    if (jsonText.compare(valueStart, 5, "false") == 0) {
        return false;
    }

    return true;
}
}

ConfigManager::ConfigManager() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    m_configPath = fs::path(exePath).parent_path() / L"config.json";
    m_stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
}

ConfigManager::~ConfigManager() {
    StopWatching();
    if (m_stopEvent) CloseHandle(m_stopEvent);
}

bool ConfigManager::Load() {
    if (!fs::exists(m_configPath)) {
        Save();
        return true;
    }

    try {
        std::ifstream fileStream(m_configPath);
        std::string jsonText((std::istreambuf_iterator<char>(fileStream)), std::istreambuf_iterator<char>());

        int microphoneVolumePercent = ParseMicrophoneVolumePercent(jsonText);
        if (microphoneVolumePercent < 0) {
            microphoneVolumePercent = 0;
        }
        if (microphoneVolumePercent > 100) {
            microphoneVolumePercent = 100;
        }

        m_config.microphoneVolume = static_cast<float>(microphoneVolumePercent) / 100.0f;
        m_config.enabled = ParseEnabled(jsonText);
        return true;
    } catch (...) {
        return false;
    }
}

void ConfigManager::Save() {
    try {
        int microphoneVolumePercent = static_cast<int>(m_config.microphoneVolume * 100.0f);
        if (microphoneVolumePercent < 0) {
            microphoneVolumePercent = 0;
        }
        if (microphoneVolumePercent > 100) {
            microphoneVolumePercent = 100;
        }

        std::ofstream fileStream(m_configPath);
        fileStream << "{\n";
        fileStream << "  \"microphoneVolume\": " << microphoneVolumePercent << ",\n";
        fileStream << "  \"enabled\": " << (m_config.enabled ? "true" : "false") << "\n";
        fileStream << "}\n";
    } catch (...) {
    }
}

const Config& ConfigManager::GetConfig() const {
    return m_config;
}

void ConfigManager::SetEnabled(bool enabled) {
    m_config.enabled = enabled;
    Save();
}

void ConfigManager::SetCallback(ConfigChangedCallback callback) {
    m_callback = callback;
}

void ConfigManager::StartWatching() {
    if (m_running) return;
    m_running = true;
    ResetEvent(m_stopEvent);
    m_watchThread = std::thread(&ConfigManager::WatchThread, this);
}

void ConfigManager::StopWatching() {
    if (!m_running) return;
    m_running = false;
    SetEvent(m_stopEvent);
    if (m_watchThread.joinable()) {
        m_watchThread.join();
    }
}

void ConfigManager::WatchThread() {
    fs::path dir = fs::path(m_configPath).parent_path();
    HANDLE hDir = CreateFileW(
        dir.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        NULL
    );

    if (hDir == INVALID_HANDLE_VALUE) return;

    OVERLAPPED overlapped = {0};
    overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    alignas(DWORD) uint8_t buffer[1024];

    while (m_running) {
        DWORD bytesReturned;
        if (ReadDirectoryChangesW(
            hDir, buffer, sizeof(buffer), FALSE,
            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME,
            &bytesReturned, &overlapped, NULL
        )) {
            HANDLE handles[] = { m_stopEvent, overlapped.hEvent };
            DWORD wait = WaitForMultipleObjects(2, handles, FALSE, INFINITE);

            if (wait == WAIT_OBJECT_0) break; // Stop event

            if (wait == WAIT_OBJECT_0 + 1) {
                // Change detected
                PFILE_NOTIFY_INFORMATION pNotify;
                DWORD offset = 0;
                bool relevantChange = false;

                do {
                    pNotify = (PFILE_NOTIFY_INFORMATION)&buffer[offset];
                    std::wstring fileName(pNotify->FileName, pNotify->FileNameLength / sizeof(wchar_t));
                    if (fileName == fs::path(m_configPath).filename().wstring()) {
                        relevantChange = true;
                    }
                    offset += pNotify->NextEntryOffset;
                } while (pNotify->NextEntryOffset != 0);

                if (relevantChange) {
                    // Debounce a bit to let the file be fully written
                    Sleep(100);
                    OnFileChanged();
                }

                ResetEvent(overlapped.hEvent);
            }
        } else {
            break;
        }
    }

    CloseHandle(overlapped.hEvent);
    CloseHandle(hDir);
}

void ConfigManager::OnFileChanged() {
    if (Load()) {
        if (m_callback) {
            m_callback(m_config);
        }
    }
}
