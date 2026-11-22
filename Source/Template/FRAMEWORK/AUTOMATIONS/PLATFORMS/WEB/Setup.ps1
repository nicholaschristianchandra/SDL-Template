. "$PSScriptRoot\..\..\SetupHelpers.ps1"
Write-Host "Web setup: Emscripten, which builds WebAssembly (about 1 GB), then SDL built with it."

Step 'Emscripten'
$emsdk = @("$Tools\emsdk") | Where-Object { Test-Path (Join-Path $_ 'emsdk.bat') }
if (-not $emsdk)
{
    $archive = Join-Path $Temporary 'emsdk.zip'
    Download 'https://github.com/emscripten-core/emsdk/archive/refs/heads/main.zip' $archive
    $emsdk = "$Tools\emsdk"
    Unpack $archive $emsdk 'emsdk-main'
}
Note "emsdk is in $emsdk"

# emsdk runs on Python: its own once installed, otherwise one on PATH, otherwise a private copy downloaded here
$ownPython = Get-ChildItem (Join-Path $emsdk 'python') -Filter python.exe -Recurse -ErrorAction SilentlyContinue
$pathPython = Get-Command python.exe -ErrorAction SilentlyContinue | Where-Object { $_.Source -notlike '*WindowsApps*' }
if (-not $ownPython -and -not $pathPython)
{
    $archive = Join-Path $Temporary 'python.zip'
    Download 'https://www.python.org/ftp/python/3.13.3/python-3.13.3-embed-amd64.zip' $archive
    Unpack $archive "$Tools\emsdk-python"
    $env:Path = "$Tools\emsdk-python;$env:Path"
}

$least = Get-Version 'EMSCRIPTEN_VERSION'
$versionFile = Join-Path $emsdk 'upstream\emscripten\emscripten-version.txt'
function Installed-Emscripten { if (Test-Path $versionFile) { (Get-Content $versionFile -Raw).Trim().Trim('"') } else { '' } }
$version = Installed-Emscripten
if (-not $version -or (Is-Older $version $least))
{
    $wanted = if ($least) { $least } else { 'latest' }
    Push-Location $emsdk
    Tool .\emsdk.bat @('install', $wanted) | Out-Host
    if ($LASTEXITCODE) { Fail 'emsdk could not install Emscripten: see the messages above.' }
    Tool .\emsdk.bat @('activate', $wanted) | Out-Host
    if ($LASTEXITCODE) { Fail 'emsdk could not activate Emscripten: see the messages above.' }
    Pop-Location
    Remove-Item -Recurse -Force (Join-Path $emsdk 'downloads') -ErrorAction SilentlyContinue
    $version = Installed-Emscripten
}
Note "Emscripten $version"

Build-Sdl Web
Clean-Up
Write-Host "`nDone: build the Web configuration again." -ForegroundColor Green
