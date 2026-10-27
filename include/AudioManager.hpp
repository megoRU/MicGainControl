#pragma once

#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <wrl/client.h>
#include <atomic>

using Microsoft::WRL::ComPtr;

class AudioManager : public IAudioEndpointVolumeCallback, public IMMNotificationClient {
public:
    // The COM notification callbacks only post these messages. All Core Audio
    // calls must run on the owning UI thread, because the callbacks are
    // delivered by the audio service and must return immediately.
    static constexpr UINT kVolumeChangedMessage = WM_APP + 2;
    static constexpr UINT kDeviceChangedMessage = WM_APP + 3;

    AudioManager();
    ~AudioManager();

    bool Initialize();
    void Shutdown();
    void SetNotificationWindow(HWND window);

    void SetTargetVolume(float volume);
    void SetEnabled(bool enabled);
    void ApplyVolumeImmediately(float volume);
    void EnforceVolume();

    // Called on the UI thread when a posted notification is processed.
    void HandleVolumeChangeNotification();
    void HandleDeviceChangeNotification();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IAudioEndpointVolumeCallback
    STDMETHODIMP OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify) override;

    // IMMNotificationClient
    STDMETHODIMP OnDeviceStateChanged([[maybe_unused]] LPCWSTR pwstrDeviceId, [[maybe_unused]] DWORD dwNewState) override;
    STDMETHODIMP OnDeviceAdded([[maybe_unused]] LPCWSTR pwstrDeviceId) override;
    STDMETHODIMP OnDeviceRemoved([[maybe_unused]] LPCWSTR pwstrDeviceId) override;
    STDMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, [[maybe_unused]] LPCWSTR pwstrDefaultDeviceId) override;
    STDMETHODIMP OnPropertyValueChanged([[maybe_unused]] LPCWSTR pwstrDeviceId, [[maybe_unused]] const PROPERTYKEY key) override { return S_OK; }

private:
    bool SetupDefaultDevice();
    void CleanupDevice();
    void PostNotification(UINT message, std::atomic<bool>& pending);

    std::atomic<long> m_refCount{1};
    std::atomic<HWND> m_notificationWindow{nullptr};
    std::atomic<bool> m_volumeChangePending{false};
    std::atomic<bool> m_deviceChangePending{false};

    // Core Audio objects and enforcement state are used only on the UI thread.
    ComPtr<IMMDeviceEnumerator> m_enumerator;
    ComPtr<IMMDevice> m_device;
    ComPtr<IAudioEndpointVolume> m_volumeControl;

    float m_targetVolume = 1.0f;
    bool m_enabled = true;

    static const GUID m_contextGuid;
};
