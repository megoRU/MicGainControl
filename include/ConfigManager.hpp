#pragma once

#include <string>
#include <functional>
#include <atomic>
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
    void SaveConfig(const Config& config);

    Config m_config;
    bool m_volumeDirty = false;
    mutable std::mutex m_configMutex;
    std::mutex m_saveMutex;
    std::wstring m_configPath;
    ConfigChangedCallback m_callback;

    std::atomic<bool> m_running{false};
    std::thread m_watchThread;
    HANDLE m_stopEvent = NULL;
};
