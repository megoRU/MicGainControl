#include "AudioManager.hpp"
#include <iostream>
#include <cmath>

// {A316E243-764D-4700-9423-93E1030F5522}
const GUID AudioManager::m_contextGuid = { 0xa316e243, 0x764d, 0x4700, { 0x94, 0x23, 0x93, 0xe1, 0x3, 0xf, 0x55, 0x22 } };

AudioManager::AudioManager() {}

AudioManager::~AudioManager() {
    CleanupDevice();
    if (m_enumerator) {
        m_enumerator->UnregisterEndpointNotificationCallback(this);
    }
}

bool AudioManager::Initialize() {
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&m_enumerator);
    if (FAILED(hr)) return false;

    hr = m_enumerator->RegisterEndpointNotificationCallback(this);
    if (FAILED(hr)) return false;

    return SetupDefaultDevice();
}

bool AudioManager::SetupDefaultDevice() {
    std::lock_guard<std::mutex> lock(m_mutex);
    CleanupDevice();

    HRESULT hr = m_enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &m_device);
    if (FAILED(hr)) return false;

    hr = m_device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL, (void**)&m_volumeControl);
    if (FAILED(hr)) return false;

    hr = m_volumeControl->RegisterControlChangeNotify(this);
    if (FAILED(hr)) return false;

    // Initial enforcement
    EnforceVolume();

    return true;
}

void AudioManager::CleanupDevice() {
    if (m_volumeControl) {
        m_volumeControl->UnregisterControlChangeNotify(this);
        m_volumeControl.Reset();
    }
    if (m_device) {
        m_device.Reset();
    }
}

void AudioManager::SetTargetVolume(float volume) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_targetVolume = volume;
    EnforceVolume();
}

void AudioManager::SetEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_enabled = enabled;
    if (m_enabled) {
        EnforceVolume();
    }
}

void AudioManager::EnforceVolume() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_enabled || !m_volumeControl) return;

    float currentVolume = 0.0f;
    m_volumeControl->GetMasterVolumeLevelScalar(&currentVolume);

    // Use a small epsilon for float comparison
    if (std::abs(currentVolume - m_targetVolume) > 0.001f) {
        m_volumeControl->SetMasterVolumeLevelScalar(m_targetVolume, &m_contextGuid);
    }

    BOOL mute = FALSE;
    m_volumeControl->GetMute(&mute);
    if (mute) {
        m_volumeControl->SetMute(FALSE, &m_contextGuid);
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
    if (pNotify->guidEventContext != m_contextGuid) {
        EnforceVolume();
    }
    return S_OK;
}

STDMETHODIMP AudioManager::OnDefaultDeviceChanged(EDataFlow flow, ERole role, [[maybe_unused]] LPCWSTR pwstrDefaultDeviceId) {
    if (flow == eCapture && role == eConsole) {
        SetupDefaultDevice();
    }
    return S_OK;
}
