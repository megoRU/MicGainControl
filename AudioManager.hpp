#pragma once

#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <wrl/client.h>
#include <atomic>
#include <mutex>

using Microsoft::WRL::ComPtr;

class AudioManager : public IAudioEndpointVolumeCallback, public IMMNotificationClient {
public:
    AudioManager();
    ~AudioManager();

    bool Initialize();
    void SetTargetVolume(float volume);
    void SetEnabled(bool enabled);
    void EnforceVolume();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IAudioEndpointVolumeCallback
    STDMETHODIMP OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify) override;

    // IMMNotificationClient
    STDMETHODIMP OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) override { return S_OK; }
    STDMETHODIMP OnDeviceAdded(LPCWSTR pwstrDeviceId) override { return S_OK; }
    STDMETHODIMP OnDeviceRemoved(LPCWSTR pwstrDeviceId) override { return S_OK; }
    STDMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId) override;
    STDMETHODIMP OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) override { return S_OK; }

private:
    bool SetupDefaultDevice();
    void CleanupDevice();

    std::atomic<long> m_refCount{1};
    ComPtr<IMMDeviceEnumerator> m_enumerator;
    ComPtr<IMMDevice> m_device;
    ComPtr<IAudioEndpointVolume> m_volumeControl;

    float m_targetVolume = 1.0f;
    bool m_enabled = true;
    std::mutex m_mutex;

    static const GUID m_contextGuid;
};
