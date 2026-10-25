// Small shell operations: open, reveal, select on the desktop, clipboard, autostart.
#pragma once
#include "common.h"

namespace ShellOps
{
bool IsDesktopWindow(HWND hwnd);
bool Open(const std::wstring& path, const std::wstring& dir, std::wstring& error);
bool Reveal(const std::wstring& path, std::wstring& error);
bool SelectOnDesktop(const std::wstring& path); // selects the icon on the real desktop
bool RunFromExplorer(const std::wstring& exe, const std::wstring& args, const std::wstring& dir); // starts exe as Explorer's child
bool CopyText(HWND owner, const std::wstring& text);
} // namespace ShellOps

namespace Autostart
{
bool IsEnabled();
void Set(bool enabled);
bool Initialize(); // on by default the first time (then returns true); afterwards keeps the entry pointing at this exe
} // namespace Autostart
