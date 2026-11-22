# Shared by every platform's Setup.ps1: dot-source it with . "$PSScriptRoot\..\..\SetupHelpers.ps1"
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
# This run's downloads, in its own folder; ones left by a closed setup window are deleted here
Get-ChildItem $env:TEMP -Directory -Filter 'SDLTemplateSetup*' -ErrorAction SilentlyContinue | Where-Object { -not ($_.Name -match '\d+$' -and (Get-Process -Id $Matches[0] -ErrorAction SilentlyContinue)) } | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
$Temporary = Join-Path $env:TEMP "SDLTemplateSetup$PID"

# TEMPLATE_TOOLS: the folder every tool is installed in, asked for once per PC
$Tools = [Environment]::GetEnvironmentVariable('TEMPLATE_TOOLS', 'User')
if (-not $Tools)
{
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog -Property @{ Description = 'Where should the tools (zig, Emscripten, the Android SDK...) be installed? Cancel keeps AppData\Local.'; SelectedPath = $env:LOCALAPPDATA }
    $owner = New-Object System.Windows.Forms.Form -Property @{ TopMost = $true }
    $Tools = if ($dialog.ShowDialog($owner) -eq 'OK') { $dialog.SelectedPath } else { $env:LOCALAPPDATA }
    $owner.Dispose()
    [Environment]::SetEnvironmentVariable('TEMPLATE_TOOLS', $Tools, 'User')
    Write-Host "   TEMPLATE_TOOLS=$Tools"
}
$env:TEMPLATE_TOOLS = $Tools

function Step([string] $Text) { Write-Host "`n== $Text" -ForegroundColor Cyan }
function Note([string] $Text) { Write-Host "   $Text" }
function Ask([string] $Question) { (Read-Host "   $Question [Y/n]") -notmatch '^\s*[nN]' }

# A tool's least version, from the calling platform's Config.bat (blank: the latest); the build records it
$PlatformConfig = Join-Path (Split-Path $MyInvocation.PSCommandPath) 'Config.bat'
function Get-Version([string] $Name) { $m = Select-String -LiteralPath $PlatformConfig -Pattern "^set `"$Name=([^`"]*)`"" | Select-Object -First 1; if ($m) { $m.Matches[0].Groups[1].Value } else { '' } }
function Is-Older([string] $Installed, [string] $Least)
{
    $parts = foreach ($text in $Installed, $Least) { [version]((@([regex]::Match($text, '\d+(\.\d+)*').Value -split '\.' | Where-Object { $_ }) + @('0', '0', '0', '0'))[0..3] -join '.') }
    $Least -and $parts[0] -lt $parts[1]
}

# Deletes the setup's downloads, done or failed
function Clean-Up { Remove-Item -Recurse -Force $Temporary -ErrorAction SilentlyContinue }

function Fail([string] $Text)
{
    Write-Host "`n   $Text" -ForegroundColor Red
    Clean-Up
    exit 1
}

# Runs a program, its stderr not taken as a failure: returns its output, $LASTEXITCODE says if it worked, Feed is its input
function Tool([string] $Program, [string[]] $Arguments = @(), [string[]] $Feed = @())
{
    $ErrorActionPreference = 'Continue'
    try { $Feed | & $Program @Arguments 2>&1 | ForEach-Object { "$_" } }
    catch { $global:LASTEXITCODE = 1; "$_" }
}

function Download([string] $Url, [string] $Path)
{
    New-Item -ItemType Directory -Force (Split-Path $Path) | Out-Null
    Note "Downloading $Url"
    Invoke-WebRequest -Uri $Url -OutFile $Path -UseBasicParsing
}

# Unpacks a .zip into Destination; with Inner, moves that folder of the archive to Destination instead
function Unpack([string] $Archive, [string] $Destination, [string] $Inner = '')
{
    $unpacked = Join-Path $Temporary ([IO.Path]::GetRandomFileName())
    New-Item -ItemType Directory -Force $unpacked | Out-Null
    Tool tar @('-xf', $Archive, '-C', $unpacked) | Out-Host
    if ($LASTEXITCODE) { Fail "Could not unpack $Archive." }
    $source = if ($Inner) { Join-Path $unpacked $Inner } else { $unpacked }
    if (Test-Path $Destination) { Remove-Item -Recurse -Force $Destination }
    New-Item -ItemType Directory -Force (Split-Path $Destination) | Out-Null
    Move-Item $source $Destination
}

# The only folder inside an unpacked archive, for archives that wrap everything in one (jdk-21.0.5+11, zig-x86_64-...)
function Only-Folder([string] $Archive)
{
    (Tool tar @('-tf', $Archive) | ForEach-Object { ($_ -split '/')[0] } | Select-Object -Unique | Select-Object -First 1)
}

# DXC, Microsoft's HLSL compiler, from its GitHub releases into TEMPLATE_TOOLS\dxc: the Windows SDK's copy can't make SPIR-V
function Install-Dxc
{
    Step 'DXC, which compiles the HLSL shaders to SPIR-V for Vulkan'
    $folder = Join-Path $Tools 'dxc'

    if (Test-Path "$folder\dxc.exe") { Note "Found $folder\dxc.exe"; return }

    $release = Invoke-RestMethod 'https://api.github.com/repos/microsoft/DirectXShaderCompiler/releases/latest'
    $asset = $release.assets | Where-Object { $_.name -match '^dxc_.*\.zip$' } | Select-Object -First 1
    if (-not $asset) { Fail 'Could not find DXC to download: try again later.' }

    $archive = Join-Path $Temporary 'dxc.zip'
    $unpacked = Join-Path $Temporary 'dxc'
    Download $asset.browser_download_url $archive
    Unpack $archive $unpacked
    $compiler = Get-ChildItem $unpacked -Recurse -Filter dxc.exe | Where-Object { $_.DirectoryName -match '[\\/]x64$' } | Select-Object -First 1
    if (-not $compiler) { Fail 'The DXC download has no x64 dxc.exe.' }

    if (Test-Path $folder) { Remove-Item -Recurse -Force $folder }
    New-Item -ItemType Directory -Force $folder | Out-Null
    Copy-Item (Join-Path $compiler.DirectoryName '*') $folder -Recurse
    Note "Installed DXC $($release.tag_name) in $folder"
}

# Builds Libraries\SDL3\lib\<Platform> from the SDL release that matches Libraries\SDL3\include
function Build-Sdl([string] $Platform)
{
    Step "SDL for $Platform"
    & cmd /c "`"$Root\Libraries\SDL3\UpdateSDL.bat`" --download $Platform --no-pause"
    if ($LASTEXITCODE) { Fail "Building SDL for $Platform failed: see the messages above." }
}

New-Item -ItemType Directory -Force $Temporary | Out-Null
