#include "audio_session.h"

#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

namespace spotify {

namespace {

template <typename T>
class ComPtr {
 public:
    ComPtr() = default;
    ~ComPtr() { reset(); }

    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    ComPtr(ComPtr&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    ComPtr& operator=(ComPtr&& o) noexcept {
        if (this != &o) { reset(); p_ = o.p_; o.p_ = nullptr; }
        return *this;
    }

    T** ptr() { reset(); return &p_; }
    T*  get() const { return p_; }
    T*  operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }

    void reset() { if (p_) { p_->Release(); p_ = nullptr; } }

 private:
    T* p_ = nullptr;
};

}  // namespace

// ------------------------------------------------------------------------

struct AudioSession::Priv {
    SpotifyClient::Impl* impl = nullptr;

    std::atomic<bool> running{false};
    std::thread worker;
    std::mutex cvMu;
    std::condition_variable cv;

    mutable std::mutex sessionMu;
    ComPtr<ISimpleAudioVolume>      volume;
    ComPtr<IAudioMeterInformation>  meter;
    ComPtr<IAudioSessionControl2>   control;
    DWORD lastBoundPid = 0;

    std::atomic<float> lastPeak{-1.0f};
    bool audibleState = false;
    int  belowCount = 0;

    void Worker();
    bool Resolve(DWORD pid);
    void ReleaseSession();
};

void AudioSession::Priv::ReleaseSession() {
    std::lock_guard lock(sessionMu);
    volume.reset();
    meter.reset();
    control.reset();
    lastBoundPid = 0;
}

bool AudioSession::Priv::Resolve(DWORD pid) {
    if (pid == 0) return false;

    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(enumerator.ptr())))) {
        return false;
    }

    ComPtr<IMMDevice> device;
    if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, device.ptr()))) {
        return false;
    }

    ComPtr<IAudioSessionManager2> mgr;
    if (FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void**>(mgr.ptr())))) {
        return false;
    }

    ComPtr<IAudioSessionEnumerator> senum;
    if (FAILED(mgr->GetSessionEnumerator(senum.ptr()))) return false;

    int count = 0;
    senum->GetCount(&count);
    for (int i = 0; i < count; ++i) {
        ComPtr<IAudioSessionControl> ctrl;
        if (FAILED(senum->GetSession(i, ctrl.ptr()))) continue;

        ComPtr<IAudioSessionControl2> ctrl2;
        if (FAILED(ctrl->QueryInterface(__uuidof(IAudioSessionControl2),
                                        reinterpret_cast<void**>(ctrl2.ptr())))) continue;

        DWORD sessionPid = 0;
        if (FAILED(ctrl2->GetProcessId(&sessionPid))) continue;
        if (sessionPid != pid) continue;

        ComPtr<ISimpleAudioVolume> vol;
        if (FAILED(ctrl->QueryInterface(__uuidof(ISimpleAudioVolume),
                                        reinterpret_cast<void**>(vol.ptr())))) continue;

        ComPtr<IAudioMeterInformation> met;
        if (FAILED(ctrl->QueryInterface(__uuidof(IAudioMeterInformation),
                                        reinterpret_cast<void**>(met.ptr())))) continue;

        std::lock_guard lock(sessionMu);
        volume  = std::move(vol);
        meter   = std::move(met);
        control = std::move(ctrl2);
        lastBoundPid = pid;
        return true;
    }
    return false;
}

void AudioSession::Priv::Worker() {
    const HRESULT coHr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool    coOwned = SUCCEEDED(coHr);

    using clock = std::chrono::steady_clock;
    auto lastResolveAttempt = clock::time_point{};

    while (running.load()) {
        const DWORD pid = impl->processId;

        bool need;
        {
            std::lock_guard lock(sessionMu);
            need = !volume || (pid != lastBoundPid);
        }

        if (pid == 0) {
            ReleaseSession();
        } else if (need && clock::now() - lastResolveAttempt > std::chrono::milliseconds(500)) {
            lastResolveAttempt = clock::now();
            Resolve(pid);  // best effort; keep polling if it fails
        }

        // Measure peak and read volume/mute under one lock.
        float peak = -1.0f;
        float vol = -1.0f;
        bool  muted = false;
        bool  haveSession = false;
        {
            std::lock_guard lock(sessionMu);
            if (volume) {
                haveSession = true;
                float v = 0.0f;
                if (SUCCEEDED(volume->GetMasterVolume(&v))) vol = v;
                BOOL m = FALSE;
                volume->GetMute(&m);
                muted = m == TRUE;
            }
            if (meter) {
                float p = 0.0f;
                if (SUCCEEDED(meter->GetPeakValue(&p))) peak = p;
            }
        }
        lastPeak.store(peak);

        constexpr float kThreshold = 0.005f;
        constexpr int   kBelowToIdle = 20;  // ~1 s at 50 ms
        bool edge = false;
        bool newAudible = audibleState;

        if (haveSession && peak >= kThreshold) {
            belowCount = 0;
            if (!audibleState) { audibleState = newAudible = true;  edge = true; }
        } else if (haveSession) {
            if (audibleState) {
                if (++belowCount >= kBelowToIdle) { audibleState = newAudible = false; edge = true; }
            }
        } else {
            if (audibleState) { audibleState = newAudible = false; belowCount = 0; edge = true; }
        }

        impl->ApplyAudio(vol, muted, newAudible, haveSession);
        if (edge) impl->self->OnAudibleChanged(newAudible);

        std::unique_lock<std::mutex> lk(cvMu);
        cv.wait_for(lk, std::chrono::milliseconds(50),
                    [this]() { return !running.load(); });
    }

    ReleaseSession();
    if (coOwned) ::CoUninitialize();
}

// ------------------------------------------------------------------------
// AudioSession
// ------------------------------------------------------------------------

AudioSession::AudioSession(SpotifyClient::Impl* impl) : p_(std::make_unique<Priv>()) {
    p_->impl = impl;
}

AudioSession::~AudioSession() { Stop(); }

void AudioSession::Start() {
    if (p_->running.exchange(true)) return;
    p_->worker = std::thread([this] { p_->Worker(); });
}

void AudioSession::Stop() {
    if (!p_->running.exchange(false)) return;
    {
        std::lock_guard<std::mutex> lock(p_->cvMu);
        p_->cv.notify_all();
    }
    if (p_->worker.joinable()) p_->worker.join();
}

bool AudioSession::HasSession() const {
    std::lock_guard lock(p_->sessionMu);
    return static_cast<bool>(p_->volume);
}

float AudioSession::GetVolume() const {
    std::lock_guard lock(p_->sessionMu);
    if (!p_->volume) return -1.0f;
    float v = 0.0f;
    return SUCCEEDED(p_->volume->GetMasterVolume(&v)) ? v : -1.0f;
}

bool AudioSession::SetVolume(float v) {
    v = (v < 0.0f) ? 0.0f : (v > 1.0f ? 1.0f : v);
    std::lock_guard lock(p_->sessionMu);
    if (!p_->volume) return false;
    return SUCCEEDED(p_->volume->SetMasterVolume(v, nullptr));
}

bool AudioSession::IsMuted() const {
    std::lock_guard lock(p_->sessionMu);
    if (!p_->volume) return false;
    BOOL m = FALSE;
    p_->volume->GetMute(&m);
    return m == TRUE;
}

bool AudioSession::SetMuted(bool muted) {
    std::lock_guard lock(p_->sessionMu);
    if (!p_->volume) return false;
    return SUCCEEDED(p_->volume->SetMute(muted ? TRUE : FALSE, nullptr));
}

float AudioSession::GetPeakAmplitude() const {
    return p_->lastPeak.load();
}

}  // namespace spotify
