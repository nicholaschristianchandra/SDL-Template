@echo off
setlocal

for %%R in ("%~dp0..\..") do set "ROOT=%%~fR"
set "BUILDS=%ROOT%\Builds"

if not exist "%BUILDS%" (
    echo [Clean] Nothing to clean.
    exit /b 0
)

echo [Clean] Closing programs running from Builds\...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$running = Get-Process | Where-Object { $_.Path -and $_.Path.StartsWith($env:BUILDS + '\', [StringComparison]::OrdinalIgnoreCase) }; $running | Stop-Process -Force; $running | Wait-Process -Timeout 10 -ErrorAction SilentlyContinue" 2>nul

echo [Clean] Deleting Builds\...
rmdir /s /q "%BUILDS%" 2>nul

if exist "%BUILDS%" (
    echo "[Clean][Error] Some files in Builds\ are in use. Close what uses them (the game, an emulator, a window open in Builds\) and clean again."
    exit /b 1
)

echo [Clean] Done. Symbols\ is kept.
exit /b 0
