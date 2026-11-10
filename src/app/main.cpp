#include <windows.h>

#include <objbase.h>

#include "app/app.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    // One instance per user session.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\WinFlake.SingleInstance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int rc = 0;
    {
        App app;
        rc = app.run(instance);
    }
    CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return rc;
}
