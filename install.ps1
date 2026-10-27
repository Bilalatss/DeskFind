# Installs or updates DeskFind for the current user, no admin rights needed:
#   irm https://raw.githubusercontent.com/Bilalatss/DeskFind/main/install.ps1 | iex
# It downloads DeskFind.exe from the latest GitHub release, checks it against the release's SHA256SUMS.txt
# and runs "DeskFind.exe --install", which copies it to %LOCALAPPDATA%\Programs\DeskFind and starts it.
# Remove it from Settings > Apps, or with: & "$env:LOCALAPPDATA\Programs\DeskFind\DeskFind.exe" --uninstall

& {
    $ErrorActionPreference = 'Stop'
    $ProgressPreference = 'SilentlyContinue' # Windows PowerShell downloads far slower while drawing progress
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    # The release's own download links, not api.github.com: the API allows 60 requests an hour per address, which a VPN
    # or a shared network uses up.
    $base = 'https://github.com/Bilalatss/DeskFind/releases/latest/download'

    function Say($text, $color = 'Gray') { Write-Host "  $text" -ForegroundColor $color }

    Write-Host ''
    Say 'DeskFind' Cyan
    if ([Environment]::OSVersion.Version.Build -lt 22000) { Say 'Note: DeskFind is made for Windows 11; on Windows 10 it may not look right.' Yellow }

    $temp = Join-Path ([IO.Path]::GetTempPath()) ('DeskFind-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $temp | Out-Null
    try {
        # The checksums come first: they also list which builds the release has.
        $sumsFile = Join-Path $temp 'SHA256SUMS.txt'
        try {
            Invoke-WebRequest "$base/SHA256SUMS.txt" -OutFile $sumsFile -UseBasicParsing
        } catch {
            if ($_.Exception.Response -and [int]$_.Exception.Response.StatusCode -eq 404) { Say 'No DeskFind release has been published yet.' Red }
            else { Say "Couldn't reach GitHub: $($_.Exception.Message)" Red }
            return
        }
        $sums = @{}
        foreach ($line in Get-Content $sumsFile) {
            if ($line -match '^([0-9a-fA-F]{64})\s+\*?(\S+)\s*$') { $sums[$Matches[2]] = $Matches[1].ToUpperInvariant() }
        }

        # Windows on Arm gets its native build (the machine's own architecture, whatever this PowerShell runs as); anything
        # else, or a release without one, the x64 build.
        $arch = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment' -ErrorAction SilentlyContinue).PROCESSOR_ARCHITECTURE
        $name = if ($arch -eq 'ARM64' -and $sums['DeskFind-arm64.exe']) { 'DeskFind-arm64.exe' } else { 'DeskFind.exe' }
        if (-not $sums[$name]) {
            Say "The latest release has no $name." Red
            return
        }

        Say 'Downloading...'
        $file = Join-Path $temp 'DeskFind.exe'
        Invoke-WebRequest "$base/$name" -OutFile $file -UseBasicParsing
        if ((Get-FileHash $file -Algorithm SHA256).Hash -ne $sums[$name]) {
            Say 'Checksum mismatch: the download is not the released file. Nothing was installed.' Red
            return
        }
        $version = 'v' + (Get-Item $file).VersionInfo.ProductVersion
        Say "Checksum OK ($version)."

        # DeskFind searches the rest of the PC through Everything (voidtools); without it the desktop and its folders are
        # still searched. Asked before DeskFind starts, so its first run already finds Everything.
        $everything = (Get-Process Everything, Everything64 -ErrorAction SilentlyContinue) -or
            (Test-Path "$env:ProgramFiles\Everything\Everything.exe") -or (Test-Path "${env:ProgramFiles(x86)}\Everything\Everything.exe")
        if (-not $everything) {
            Say 'DeskFind searches the rest of the PC through Everything by voidtools (free).' Yellow
            if (Get-Command winget -ErrorAction SilentlyContinue) {
                $answer = Read-Host '  Install Everything now with winget? [Y/n]'
                if ($answer -notmatch '^\s*n') {
                    winget install --id voidtools.Everything --exact --source winget --accept-source-agreements --accept-package-agreements
                }
            } else {
                Say 'Get it from https://www.voidtools.com/downloads/ (DeskFind also offers it later).' Yellow
            }
        }

        # Only the installer is waited for; it hands the running app over to Explorer.
        $process = Start-Process $file -ArgumentList '--install' -PassThru
        $null = $process.Handle # keeps the exit code readable after the process ends
        $process.WaitForExit()
        switch ($process.ExitCode) {
            0 { Say "Installed $version. Press Ctrl+F on the desktop to search." Green }
            1 { Say 'A running DeskFind did not close. Quit it from its tray icon and try again.' Red; return }
            2 { Say "Couldn't copy DeskFind.exe to $env:LOCALAPPDATA\Programs\DeskFind." Red; return }
            3 { Say 'Installed, but it did not start. Start DeskFind from the Start menu search.' Yellow }
            default { Say "The installer stopped with code $($process.ExitCode)." Red; return }
        }
    } finally {
        Remove-Item $temp -Recurse -Force -ErrorAction SilentlyContinue
    }
    Write-Host ''
}
