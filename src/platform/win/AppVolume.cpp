#define NOMINMAX
#include <windows.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>

#include "platform/AppVolume.h"

#include <winrt/base.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace {
bool isProgram(DWORD pid, const wchar_t *exeName)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return false;
    wchar_t path[MAX_PATH];
    DWORD size = MAX_PATH;
    const bool ok = QueryFullProcessImageNameW(process, 0, path, &size);
    CloseHandle(process);
    if (!ok)
        return false;
    const wchar_t *name = wcsrchr(path, L'\\');
    return _wcsicmp(name ? name + 1 : path, exeName) == 0;
}

bool pressKey(WORD key, int times)
{
    std::vector<INPUT> inputs;
    for (int i = 0; i < times; ++i) {
        INPUT down{};
        down.type = INPUT_KEYBOARD;
        down.ki.wVk = key;
        INPUT up = down;
        up.ki.dwFlags = KEYEVENTF_KEYUP;
        inputs.push_back(down);
        inputs.push_back(up);
    }
    return SendInput(UINT(inputs.size()), inputs.data(), sizeof(INPUT)) == inputs.size();
}

template<typename Fn>
bool withOutputDevice(Fn fn)
{
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool ok = false;
    try {
        auto enumerator = winrt::create_instance<IMMDeviceEnumerator>(__uuidof(MMDeviceEnumerator), CLSCTX_ALL);
        winrt::com_ptr<IMMDevice> device;
        winrt::check_hresult(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.put()));
        ok = fn(device.get());
    } catch (const winrt::hresult_error &) {
    }
    if (SUCCEEDED(init))
        CoUninitialize();
    return ok;
}

template<typename Fn>
bool withSystemVolume(Fn fn)
{
    return withOutputDevice([&](IMMDevice *device) {
        winrt::com_ptr<IAudioEndpointVolume> volume;
        winrt::check_hresult(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, volume.put_void()));
        return fn(volume.get());
    });
}

// false: Spotify has no audio session right now
template<typename Fn>
bool withSpotifySessions(Fn fn)
{
    return withOutputDevice([&](IMMDevice *device) {
        winrt::com_ptr<IAudioSessionManager2> manager;
        winrt::check_hresult(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, manager.put_void()));
        winrt::com_ptr<IAudioSessionEnumerator> sessions;
        winrt::check_hresult(manager->GetSessionEnumerator(sessions.put()));
        int count = 0;
        sessions->GetCount(&count);
        bool found = false;
        for (int i = 0; i < count; ++i) {
            winrt::com_ptr<IAudioSessionControl> control;
            if (FAILED(sessions->GetSession(i, control.put())))
                continue;
            auto control2 = control.try_as<IAudioSessionControl2>();
            DWORD pid = 0;
            if (!control2 || FAILED(control2->GetProcessId(&pid)) || !isProgram(pid, L"Spotify.exe"))
                continue;
            if (auto volume = control.try_as<ISimpleAudioVolume>()) {
                fn(volume.get());
                found = true;
            }
        }
        return found;
    });
}
}

namespace AppVolume {
bool setSystem(int percent)
{
    return withSystemVolume([percent](IAudioEndpointVolume *volume) {
        if (percent > 0)
            volume->SetMute(FALSE, nullptr);
        return SUCCEEDED(volume->SetMasterVolumeLevelScalar(std::clamp(percent, 0, 100) / 100.0f, nullptr));
    });
}

bool stepSystem(int delta)
{
    const int presses = (std::abs(delta) + 1) / 2; // one key press is 2 %
    return delta == 0 || pressKey(delta > 0 ? VK_VOLUME_UP : VK_VOLUME_DOWN, presses);
}

bool toggleSystemMute()
{
    return pressKey(VK_VOLUME_MUTE, 1);
}

bool setSpotify(int percent)
{
    return withSpotifySessions([percent](ISimpleAudioVolume *volume) {
        volume->SetMasterVolume(std::clamp(percent, 0, 100) / 100.0f, nullptr);
    });
}

int spotifyLevel()
{
    int result = -1;
    withSpotifySessions([&result](ISimpleAudioVolume *volume) {
        float level = 0.0f;
        if (result < 0 && SUCCEEDED(volume->GetMasterVolume(&level)))
            result = int(std::lround(level * 100));
    });
    return result;
}

}
