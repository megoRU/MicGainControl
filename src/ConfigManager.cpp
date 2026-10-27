#include "ConfigManager.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <shlobj.h>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {
std::string FindJsonValue(const std::string& json, const char* key) {
    const std::string quotedKey = std::string("\"") + key + "\"";
    const std::size_t keyPosition = json.find(quotedKey);
    if (keyPosition == std::string::npos) return {};
    const std::size_t colon = json.find(':', keyPosition + quotedKey.size());
    if (colon == std::string::npos) return {};
    const std::size_t start = json.find_first_not_of(" \t\r\n", colon + 1);
    if (start == std::string::npos) return {};
    const std::size_t end = json.find_first_of(",}\r\n \t", start);
    return json.substr(start, end == std::string::npos ? end : end - start);
}

bool ParseConfig(const std::string& json, Config& config) {
    if (json.find('{') == std::string::npos || json.find('}') == std::string::npos) return false;

    const std::string volume = FindJsonValue(json, "microphoneVolume");
    if (!volume.empty()) {
        try {
            std::size_t parsed = 0;
            const double value = std::stod(volume, &parsed);
            if (parsed != volume.size() || !std::isfinite(value)) return false;
            // Existing releases serialize a whole-number percentage. Also accept
            // explicit fractional scalars (for example 0.85) used by older docs.
            const double scalar = (volume.find('.') != std::string::npos ||
                volume.find('e') != std::string::npos || volume.find('E') != std::string::npos)
                ? value : value / 100.0;
            config.microphoneVolume = static_cast<float>(std::clamp(scalar, 0.0, 1.0));
        } catch (...) {
            return false;
        }
    }

    const std::string enabled = FindJsonValue(json, "enabled");
    if (!enabled.empty()) {
        if (enabled == "true") config.enabled = true;
        else if (enabled == "false") config.enabled = false;
        else return false;
    }
    return true;
}
}

ConfigManager::ConfigManager() {
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &path))) {
        m_configPath = std::wstring(path) + L"\\.MicGainControl.json";
        CoTaskMemFree(path);
    } else {
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        m_configPath = fs::path(exePath).parent_path() / L".MicGainControl.json";
    }
    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

ConfigManager::~ConfigManager() {
    StopWatching();
    if (m_stopEvent) CloseHandle(m_stopEvent);
}

bool ConfigManager::Load() {
    std::error_code error;
    if (!fs::exists(m_configPath, error)) {
        if (error) return false;
        Save();
        return true;
    }

    try {
        std::ifstream stream(fs::path(m_configPath), std::ios::binary);
        if (!stream) return false;
        const std::string json((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        Config loaded;
        if (!ParseConfig(json, loaded)) return false;
        std::lock_guard<std::mutex> lock(m_configMutex);
        m_config = loaded;
        m_volumeDirty = false;
        return true;
    } catch (...) {
        return false;
    }
}

void ConfigManager::SaveThread() {
    for (;;) {
        Config config;
        {
            std::unique_lock<std::mutex> lock(m_saveMutex);
            m_saveCondition.wait(lock, [this]() {
                return m_stopSaveThread || m_savePending;
            });
            if (!m_savePending) {
                if (m_stopSaveThread) break;
                continue;
            }
            config = m_pendingSave;
            m_savePending = false;
        }
        WriteConfigFile(config);
    }
}

void ConfigManager::StartSaveThread() {
    std::lock_guard<std::mutex> lock(m_saveMutex);
    if (m_saveWorkerRunning) return;

    m_stopSaveThread = false;
    m_saveWorkerRunning = true;
    try {
        m_saveThread = std::thread(&ConfigManager::SaveThread, this);
    } catch (...) {
        m_saveWorkerRunning = false;
        OutputDebugStringW(L"MicGainControl: unable to start the configuration save thread.\n");
    }
}

void ConfigManager::StopSaveThread() {
    {
        std::lock_guard<std::mutex> lock(m_saveMutex);
        m_stopSaveThread = true;
    }
    m_saveCondition.notify_all();

    if (m_saveThread.joinable()) {
        m_saveThread.join();
    }

    Config pending;
    bool hasPending = false;
    {
        std::lock_guard<std::mutex> lock(m_saveMutex);
        m_saveWorkerRunning = false;
        // A caller could have queued a save while the worker was finishing.
        if (m_savePending) {
            pending = m_pendingSave;
            m_savePending = false;
            hasPending = true;
        }
        m_stopSaveThread = false;
    }
    if (hasPending) {
        WriteConfigFile(pending);
    }
}

void ConfigManager::QueueSave(const Config& config) {
    {
        std::lock_guard<std::mutex> lock(m_saveMutex);
        if (m_saveWorkerRunning) {
            m_pendingSave = config;
            m_savePending = true;
            m_saveCondition.notify_one();
            return;
        }
    }

    // Configuration is also written before StartWatching() creates the worker
    // (for example, when creating the first-run settings file).
    WriteConfigFile(config);
}

void ConfigManager::WriteConfigFile(const Config& config) {
    std::lock_guard<std::mutex> writeLock(m_writeMutex);
    try {
        const int volumePercent = static_cast<int>(std::lround(std::clamp(config.microphoneVolume, 0.0f, 1.0f) * 100.0f));
        const fs::path path(m_configPath);
        const fs::path temporaryPath = path.wstring() + L".tmp";
        {
            std::ofstream stream(temporaryPath, std::ios::binary | std::ios::trunc);
            if (!stream) {
                OutputDebugStringW(L"MicGainControl: unable to write the configuration temporary file.\n");
                return;
            }
            stream << "{\n  \"microphoneVolume\": " << volumePercent
                   << ",\n  \"enabled\": " << (config.enabled ? "true" : "false") << "\n}\n";
            if (!stream) {
                OutputDebugStringW(L"MicGainControl: configuration write failed.\n");
                return;
            }
        }
        if (!MoveFileExW(temporaryPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            OutputDebugStringW(L"MicGainControl: unable to replace the configuration file.\n");
            DeleteFileW(temporaryPath.c_str());
        }
    } catch (...) {
    }
}

void ConfigManager::Save() {
    QueueSave(GetConfig());
}

Config ConfigManager::GetConfig() const {
    std::lock_guard<std::mutex> lock(m_configMutex);
    return m_config;
}

void ConfigManager::SetEnabled(bool enabled) {
    Config config;
    {
        std::lock_guard<std::mutex> lock(m_configMutex);
        if (m_config.enabled == enabled) return;
        m_config.enabled = enabled;
        m_volumeDirty = false; // This write includes any in-progress preview value.
        config = m_config;
    }
    QueueSave(config);
}

void ConfigManager::SetMicrophoneVolume(float volume, bool save) {
    if (!std::isfinite(volume)) return;
    Config config;
    bool shouldSave = false;
    {
        std::lock_guard<std::mutex> lock(m_configMutex);
        volume = std::clamp(volume, 0.0f, 1.0f);
        if (std::abs(m_config.microphoneVolume - volume) >= 0.0001f) {
            m_config.microphoneVolume = volume;
            m_volumeDirty = true;
        }
        config = m_config;
        if (save && m_volumeDirty) {
            m_volumeDirty = false;
            shouldSave = true;
        }
    }
    if (shouldSave) QueueSave(config);
}

void ConfigManager::SetCallback(ConfigChangedCallback callback) {
    std::lock_guard<std::mutex> lock(m_configMutex);
    m_callback = std::move(callback);
}

void ConfigManager::StartWatching() {
    StartSaveThread();

    if (!m_stopEvent) {
        OutputDebugStringW(L"MicGainControl: unable to create the configuration watcher stop event.\n");
        return;
    }
    if (m_running.exchange(true)) return;
    if (m_watchThread.joinable()) m_watchThread.join();
    ResetEvent(m_stopEvent);
    m_watchThread = std::thread(&ConfigManager::WatchThread, this);
}

void ConfigManager::StopWatching() {
    m_running = false;
    if (m_stopEvent) SetEvent(m_stopEvent);
    if (m_watchThread.joinable()) m_watchThread.join();
    StopSaveThread();
}

void ConfigManager::WatchThread() {
    const fs::path directory = fs::path(m_configPath).parent_path();
    HANDLE directoryHandle = CreateFileW(directory.c_str(), FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (directoryHandle == INVALID_HANDLE_VALUE) {
        m_running = false;
        return;
    }

    OVERLAPPED overlapped = {};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent) {
        CloseHandle(directoryHandle);
        m_running = false;
        return;
    }

    std::vector<BYTE> buffer(16 * 1024);
    const std::wstring targetName = fs::path(m_configPath).filename().wstring();
    while (m_running) {
        ResetEvent(overlapped.hEvent);
        const BOOL started = ReadDirectoryChangesW(directoryHandle, buffer.data(),
            static_cast<DWORD>(buffer.size()), FALSE,
            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME,
            nullptr, &overlapped, nullptr);
        if (!started && GetLastError() != ERROR_IO_PENDING) break;

        HANDLE handles[] = { m_stopEvent, overlapped.hEvent };
        const DWORD wait = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (wait == WAIT_OBJECT_0) {
            CancelIoEx(directoryHandle, &overlapped);
            WaitForSingleObject(overlapped.hEvent, INFINITE);
            DWORD cancelledBytes = 0;
            GetOverlappedResult(directoryHandle, &overlapped, &cancelledBytes, FALSE);
            break;
        }
        if (wait != WAIT_OBJECT_0 + 1) {
            CancelIoEx(directoryHandle, &overlapped);
            WaitForSingleObject(overlapped.hEvent, INFINITE);
            DWORD cancelledBytes = 0;
            GetOverlappedResult(directoryHandle, &overlapped, &cancelledBytes, FALSE);
            break;
        }

        DWORD bytes = 0;
        const BOOL completed = GetOverlappedResult(directoryHandle, &overlapped, &bytes, FALSE);
        const DWORD completionError = completed ? ERROR_SUCCESS : GetLastError();
        if (!completed && completionError != ERROR_NOTIFY_ENUM_DIR) break;
        bool changed = completionError == ERROR_NOTIFY_ENUM_DIR;
        for (DWORD offset = 0; offset < bytes;) {
            const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer.data() + offset);
            const std::wstring fileName(info->FileName, info->FileNameLength / sizeof(wchar_t));
            if (_wcsicmp(fileName.c_str(), targetName.c_str()) == 0) changed = true;
            if (info->NextEntryOffset == 0) break;
            offset += info->NextEntryOffset;
        }
        if (changed) {
            // Editors often replace the file atomically; brief debounce avoids
            // reading while the replacement is still being committed.
            if (WaitForSingleObject(m_stopEvent, 100) == WAIT_OBJECT_0) break;
            OnFileChanged();
        }
    }

    CloseHandle(overlapped.hEvent);
    CloseHandle(directoryHandle);
    m_running = false;
}

void ConfigManager::OnFileChanged() {
    if (!Load()) return; // Keep the last known-good configuration on malformed writes.
    ConfigChangedCallback callback;
    {
        std::lock_guard<std::mutex> lock(m_configMutex);
        callback = m_callback;
    }
    if (callback) callback(GetConfig());
}
