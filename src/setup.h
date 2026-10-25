// Per-user install without an installer: "DeskFind.exe --install" copies itself to %LOCALAPPDATA%\Programs\DeskFind,
// adds a Start menu entry, registers in Settings > Apps and starts; "--uninstall" (what Settings runs) removes the exe,
// the entry, its settings and its data.
#pragma once
#include "common.h"

namespace Setup
{
// Exit codes for install.ps1: 0 done, 1 a running DeskFind didn't quit, 2 the exe couldn't be copied, 3 it didn't start.
int Install();
int Uninstall(bool quiet);
} // namespace Setup
