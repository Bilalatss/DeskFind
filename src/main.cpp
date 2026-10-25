#include "common.h"

#include <objbase.h>
#include <shellapi.h>

#include "setup.h"

int RunSelfTest(int argc, wchar_t** argv);
int RunApp(bool show);

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int code;
    const wchar_t* command = argc >= 2 ? argv[1] : L"";
    if (wcscmp(command, L"--selftest") == 0)
    {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        code = RunSelfTest(argc - 1, argv + 1);
    }
    else if (wcscmp(command, L"--install") == 0)
    {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        code = Setup::Install();
    }
    else if (wcscmp(command, L"--uninstall") == 0)
    {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        code = Setup::Uninstall(argc >= 3 && wcscmp(argv[2], L"--quiet") == 0);
    }
    else
    {
        // Started by hand (Start menu, the exe): show the box. Autostart and the installer pass --background.
        code = RunApp(wcscmp(command, L"--background") != 0);
    }
    LocalFree(argv);
    return code;
}
