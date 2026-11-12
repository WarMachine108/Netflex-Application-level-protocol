# Run from PowerShell: .\build.ps1
# The GUI and session threads use the same protocol and socket modules.
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    $options = @('-std=c++17', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-DNETFLEX_DESKTOP', '-DUNICODE', '-D_UNICODE')
    $sources = @('src/desktop.cpp', 'src/desktop_session.cpp', 'src/network.cpp', 'src/protocol.cpp')
    & g++ @options -municode -mwindows -static -static-libgcc -static-libstdc++ @sources -lws2_32 -lgdi32 -luser32 -ldwmapi -o NetFlex.exe
    if ($LASTEXITCODE -ne 0) { throw 'Desktop build failed. Check compiler output; close NetFlex.exe if it is running.' }
    Write-Host 'Ready: double-click NetFlex.exe. Both integrated terminals connect automatically.'
} finally {
    Pop-Location
}
