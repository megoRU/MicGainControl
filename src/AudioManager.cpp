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
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_enumerator) {
        LogAudioFailure(L"UnregisterEndpointNotificationCallback",
            m_enumerator->UnregisterEndpointNotificationCallback(this));
    }
    CleanupDevice();
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
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
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
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!std::isfinite(volume)) return;
    m_targetVolume = std::clamp(volume, 0.0f, 1.0f);
    EnforceVolume();
}

void AudioManager::SetEnabled(bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_enabled = enabled;
    if (m_enabled) {
        EnforceVolume();
    }
}

void AudioManager::ApplyVolumeImmediately(float volume) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!std::isfinite(volume)) return;

    if (volume < 0.0f) {
        volume = 0.0f;
    }
    if (volume > 1.0f) {
        volume = 1.0f;
    }

    m_targetVolume = volume;

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
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
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
    if (pNotify && pNotify->guidEventContext != m_contextGuid) {
        EnforceVolume();
    }
    return S_OK;
}

STDMETHODIMP AudioManager::OnDefaultDeviceChanged(EDataFlow flow, ERole role, [[maybe_unused]] LPCWSTR pwstrDefaultDeviceId) {
    if (flow == eCapture && (role == eConsole || role == eMultimedia)) {
        SetupDefaultDevice();
    }
    return S_OK;
}

STDMETHODIMP AudioManager::OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) {
    bool selectedDeviceChanged = (dwNewState & DEVICE_STATE_ACTIVE) != 0;
    if (!selectedDeviceChanged && pwstrDeviceId) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_device) {
            LPWSTR currentId = nullptr;
            const HRESULT hr = m_device->GetId(&currentId);
            if (SUCCEEDED(hr) && currentId) {
                selectedDeviceChanged = _wcsicmp(currentId, pwstrDeviceId) == 0;
            }
            if (currentId) CoTaskMemFree(currentId);
        }
    }
    if (selectedDeviceChanged) SetupDefaultDevice();
    return S_OK;
}

STDMETHODIMP AudioManager::OnDeviceAdded([[maybe_unused]] LPCWSTR pwstrDeviceId) {
    SetupDefaultDevice();
    return S_OK;
}

STDMETHODIMP AudioManager::OnDeviceRemoved(LPCWSTR pwstrDeviceId) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_device || !pwstrDeviceId) return S_OK;
    LPWSTR currentId = nullptr;
    const HRESULT hr = m_device->GetId(&currentId);
    if (SUCCEEDED(hr) && currentId && _wcsicmp(currentId, pwstrDeviceId) == 0) {
        CoTaskMemFree(currentId);
        SetupDefaultDevice();
    } else if (currentId) {
        CoTaskMemFree(currentId);
    }
    return S_OK;
}
