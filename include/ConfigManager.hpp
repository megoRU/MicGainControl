#pragma once

#include <string>
#include <functional>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <windows.h>

struct Config {
    float microphoneVolume = 1.0f; // 0.0 to 1.0
    bool enabled = true;
};

class ConfigManager {
public:
    ConfigManager();
    ~ConfigManager();

    bool Load();
    void Save();
    Config GetConfig() const;
    const std::wstring& GetConfigPath() const { return m_configPath; }
    void SetEnabled(bool enabled);
    void SetMicrophoneVolume(float volume, bool save = true);

    using ConfigChangedCallback = std::function<void(const Config&)>;
    void SetCallback(ConfigChangedCallback callback);

    void StartWatching();
    void StopWatching();

private:
    void WatchThread();
    void OnFileChanged();
    void SaveThread();
    void StartSaveThread();
    void StopSaveThread();
    void QueueSave(const Config& config);
    void WriteConfigFile(const Config& config);

    Config m_config;
    bool m_volumeDirty = false;
    mutable std::mutex m_configMutex;
    std::wstring m_configPath;
    ConfigChangedCallback m_callback;

    std::mutex m_saveMutex;
    std::mutex m_writeMutex;
    std::condition_variable m_saveCondition;
    Config m_pendingSave;
    bool m_savePending = false;
    bool m_stopSaveThread = false;
    bool m_saveWorkerRunning = false;
    std::thread m_saveThread;

    std::atomic<bool> m_running{false};
    std::thread m_watchThread;
    HANDLE m_stopEvent = NULL;
};
