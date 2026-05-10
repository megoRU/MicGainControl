#include "ConfigManager.hpp"
#include <fstream>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

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
        std::ifstream f(m_configPath);
        nlohmann::json data = nlohmann::json::parse(f);

        m_config.microphoneVolume = data.value("microphoneVolume", 100) / 100.0f;
        m_config.enabled = data.value("enabled", true);
        return true;
    } catch (...) {
        return false;
    }
}

void ConfigManager::Save() {
    try {
        nlohmann::json data;
        data["microphoneVolume"] = static_cast<int>(m_config.microphoneVolume * 100);
        data["enabled"] = m_config.enabled;

        std::ofstream f(m_configPath);
        f << data.dump(2);
    } catch (...) {}
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

    uint8_t buffer[1024];

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
