#pragma once

#include <string>
#include <functional>
#include <atomic>
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
    const Config& GetConfig() const;
    const std::wstring& GetConfigPath() const { return m_configPath; }
    void SetEnabled(bool enabled);

    using ConfigChangedCallback = std::function<void(const Config&)>;
    void SetCallback(ConfigChangedCallback callback);

    void StartWatching();
    void StopWatching();

private:
    void WatchThread();
    void OnFileChanged();

    Config m_config;
    std::wstring m_configPath;
    ConfigChangedCallback m_callback;

    std::atomic<bool> m_running{false};
    std::thread m_watchThread;
    HANDLE m_stopEvent = NULL;
};
