#include "AudioManager.hpp"
#include <algorithm>
#include <cmath>
#include <cwchar>

namespace {
void LogAudioFailure(const wchar_t* operation, HRESULT result) {
    if (FAILED(result)) {
        wchar_t message[160] = {};
        swprintf_s(message, L"MicGainControl: %s failed (0x%08X)\n", operation,
            static_cast<unsigned int>(result));
        OutputDebugStringW(message);
    }
}
}

// {A316E243-764D-4700-9423-93E1030F5522}
const GUID AudioManager::m_contextGuid = { 0xa316e243, 0x764d, 0x4700, { 0x94, 0x23, 0x93, 0xe1, 0x3, 0xf, 0x55, 0x22 } };

AudioManager::AudioManager() {}

AudioManager::~AudioManager() {
    Shutdown();
}

void AudioManager::Shutdown() {
    // Stop delivering notifications before the window and the audio objects go
    // away. Callbacks themselves never touch these objects.
    m_notificationWindow.store(nullptr, std::memory_order_release);

    if (m_enumerator) {
        LogAudioFailure(L"UnregisterEndpointNotificationCallback",
            m_enumerator->UnregisterEndpointNotificationCallback(this));
        m_enumerator.Reset();
    }
    CleanupDevice();
}

void AudioManager::SetNotificationWindow(HWND window) {
    m_notificationWindow.store(window, std::memory_order_release);
}

bool AudioManager::Initialize() {
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(m_enumerator.GetAddressOf()));
    if (FAILED(hr)) {
        LogAudioFailure(L"Create MMDeviceEnumerator", hr);
        return false;
    }

    hr = m_enumerator->RegisterEndpointNotificationCallback(this);
    if (FAILED(hr)) {
        LogAudioFailure(L"RegisterEndpointNotificationCallback", hr);
        return false;
    }

    // Keep the tray utility alive if Windows currently has no recording
    // endpoint. Device-added/default-change notifications will retry setup.
    SetupDefaultDevice();
    return true;
}

bool AudioManager::SetupDefaultDevice() {
    // This function must only be called on the UI thread. IMMNotificationClient
    // callbacks delegate here through kDeviceChangedMessage.
    CleanupDevice();

    if (!m_enumerator) return false;
    HRESULT hr = m_enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &m_device);
    if (FAILED(hr)) {
        LogAudioFailure(L"GetDefaultAudioEndpoint", hr);
        return false;
    }

    hr = m_device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
        reinterpret_cast<void**>(m_volumeControl.GetAddressOf()));
    if (FAILED(hr)) {
        LogAudioFailure(L"Activate IAudioEndpointVolume", hr);
        CleanupDevice();
        return false;
    }

    hr = m_volumeControl->RegisterControlChangeNotify(this);
    if (FAILED(hr)) {
        LogAudioFailure(L"RegisterControlChangeNotify", hr);
        CleanupDevice();
        return false;
    }

    // Initial enforcement
    EnforceVolume();

    return true;
}

void AudioManager::CleanupDevice() {
    if (m_volumeControl) {
        LogAudioFailure(L"UnregisterControlChangeNotify",
            m_volumeControl->UnregisterControlChangeNotify(this));
        m_volumeControl.Reset();
    }
    if (m_device) {
        m_device.Reset();
    }
}

void AudioManager::SetTargetVolume(float volume) {
    if (!std::isfinite(volume)) return;
    m_targetVolume = std::clamp(volume, 0.0f, 1.0f);
    EnforceVolume();
}

void AudioManager::SetEnabled(bool enabled) {
    m_enabled = enabled;
    if (m_enabled) {
        EnforceVolume();
    }
}

void AudioManager::ApplyVolumeImmediately(float volume) {
    if (!std::isfinite(volume)) return;

    m_targetVolume = std::clamp(volume, 0.0f, 1.0f);

    if (!m_volumeControl) {
        return;
    }

    LogAudioFailure(L"SetMasterVolumeLevelScalar",
        m_volumeControl->SetMasterVolumeLevelScalar(m_targetVolume, &m_contextGuid));

    BOOL mute = FALSE;
    HRESULT hr = m_volumeControl->GetMute(&mute);
    LogAudioFailure(L"GetMute", hr);
    if (mute) {
        LogAudioFailure(L"SetMute", m_volumeControl->SetMute(FALSE, &m_contextGuid));
    }
}

void AudioManager::EnforceVolume() {
    if (!m_enabled || !m_volumeControl) return;

    float currentVolume = 0.0f;
    HRESULT hr = m_volumeControl->GetMasterVolumeLevelScalar(&currentVolume);
    if (FAILED(hr)) {
        LogAudioFailure(L"GetMasterVolumeLevelScalar", hr);
        return;
    }

    // Use a small epsilon for float comparison
    if (std::abs(currentVolume - m_targetVolume) > 0.001f) {
        LogAudioFailure(L"SetMasterVolumeLevelScalar",
            m_volumeControl->SetMasterVolumeLevelScalar(m_targetVolume, &m_contextGuid));
    }

    BOOL mute = FALSE;
    hr = m_volumeControl->GetMute(&mute);
    LogAudioFailure(L"GetMute", hr);
    if (mute) {
        LogAudioFailure(L"SetMute", m_volumeControl->SetMute(FALSE, &m_contextGuid));
    }
}

void AudioManager::HandleVolumeChangeNotification() {
    m_volumeChangePending.store(false, std::memory_order_release);
    EnforceVolume();
}

void AudioManager::HandleDeviceChangeNotification() {
    m_deviceChangePending.store(false, std::memory_order_release);
    SetupDefaultDevice();
}

void AudioManager::PostNotification(UINT message, std::atomic<bool>& pending) {
    // Coalesce bursts of notifications and never wait for the UI thread.
    if (pending.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    const HWND window = m_notificationWindow.load(std::memory_order_acquire);
    if (!window || !PostMessageW(window, message, 0, 0)) {
        pending.store(false, std::memory_order_release);
    }
}

STDMETHODIMP AudioManager::QueryInterface(REFIID riid, void** ppv) {
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudioEndpointVolumeCallback)) {
        *ppv = static_cast<IAudioEndpointVolumeCallback*>(this);
    } else if (riid == __uuidof(IMMNotificationClient)) {
        *ppv = static_cast<IMMNotificationClient*>(this);
    } else {
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) AudioManager::AddRef() {
    return ++m_refCount;
}

STDMETHODIMP_(ULONG) AudioManager::Release() {
    long count = --m_refCount;
    // Since we are using this as a member in main, we don't delete this
    return count;
}

STDMETHODIMP AudioManager::OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify) {
    if (!pNotify) {
        return E_INVALIDARG;
    }

    // The audio service invokes this callback while holding its own state.
    // Calling SetMasterVolumeLevelScalar/SetMute from here can deadlock, and
    // waiting on a mutex is forbidden, so defer enforcement to the UI thread.
    if (pNotify->guidEventContext != m_contextGuid) {
        PostNotification(kVolumeChangedMessage, m_volumeChangePending);
    }
    return S_OK;
}

STDMETHODIMP AudioManager::OnDefaultDeviceChanged(EDataFlow flow, ERole role, [[maybe_unused]] LPCWSTR pwstrDefaultDeviceId) {
    if (flow == eCapture && (role == eConsole || role == eMultimedia)) {
        PostNotification(kDeviceChangedMessage, m_deviceChangePending);
    }
    return S_OK;
}

STDMETHODIMP AudioManager::OnDeviceStateChanged([[maybe_unused]] LPCWSTR pwstrDeviceId, [[maybe_unused]] DWORD dwNewState) {
    // Do not call IMMDevice/IMMDeviceEnumerator methods or wait on locks in a
    // device callback: Windows documents both as deadlock-prone.
    PostNotification(kDeviceChangedMessage, m_deviceChangePending);
    return S_OK;
}

STDMETHODIMP AudioManager::OnDeviceAdded([[maybe_unused]] LPCWSTR pwstrDeviceId) {
    PostNotification(kDeviceChangedMessage, m_deviceChangePending);
    return S_OK;
}

STDMETHODIMP AudioManager::OnDeviceRemoved([[maybe_unused]] LPCWSTR pwstrDeviceId) {
    PostNotification(kDeviceChangedMessage, m_deviceChangePending);
    return S_OK;
}
