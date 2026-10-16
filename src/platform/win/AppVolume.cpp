#define NOMINMAX
#include <windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>

#include "platform/AppVolume.h"

#include <winrt/base.h>

#include <algorithm>

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
} // namespace

namespace AppVolume {

bool setSpotify(int percent)
{
    const wchar_t *exeName = L"Spotify.exe";
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool found = false;
    try {
        auto enumerator = winrt::create_instance<IMMDeviceEnumerator>(__uuidof(MMDeviceEnumerator), CLSCTX_ALL);
        winrt::com_ptr<IMMDevice> device;
        winrt::check_hresult(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.put()));
        winrt::com_ptr<IAudioSessionManager2> manager;
        winrt::check_hresult(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, manager.put_void()));
        winrt::com_ptr<IAudioSessionEnumerator> sessions;
        winrt::check_hresult(manager->GetSessionEnumerator(sessions.put()));
        int count = 0;
        sessions->GetCount(&count);
        for (int i = 0; i < count; ++i) {
            winrt::com_ptr<IAudioSessionControl> control;
            if (FAILED(sessions->GetSession(i, control.put())))
                continue;
            auto control2 = control.try_as<IAudioSessionControl2>();
            DWORD pid = 0;
            if (!control2 || FAILED(control2->GetProcessId(&pid)) || !isProgram(pid, exeName))
                continue;
            if (auto volume = control.try_as<ISimpleAudioVolume>()) {
                volume->SetMasterVolume(std::clamp(percent, 0, 100) / 100.0f, nullptr);
                found = true;
            }
        }
    } catch (const winrt::hresult_error &) {
    }
    if (SUCCEEDED(init))
        CoUninitialize();
    return found;
}

} // namespace AppVolume
