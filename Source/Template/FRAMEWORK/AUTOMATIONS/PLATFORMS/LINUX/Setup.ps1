. "$PSScriptRoot\..\..\SetupHelpers.ps1"
Write-Host "Linux setup: zig, which builds Linux programs on Windows, DXC for the shaders, and WSL, which runs them here."

Step 'zig'
$least = Get-Version 'ZIG_VERSION'
$zig = @("$Tools\zig\zig.exe") | Where-Object { Test-Path $_ }
$version = if ($zig) { (Tool $zig @('version')) -join '' }
if ($zig -and -not (Is-Older $version $least)) { Note "Found $zig ($version)" }
else
{
    $index = Invoke-RestMethod 'https://ziglang.org/download/index.json'
    $version = if ($least) { $least } else { $index.PSObject.Properties.Name | Where-Object { $_ -match '^\d+\.\d+\.\d+$' } | Sort-Object { [version]$_ } | Select-Object -Last 1 }
    $archive = Join-Path $Temporary 'zig.zip'
    Download $index.$version.'x86_64-windows'.tarball $archive
    Unpack $archive "$Tools\zig" (Only-Folder $archive)
    Note "Installed zig $version in $Tools\zig"
}

Install-Dxc

Step 'WSL, to run Linux builds on this PC'
$env:WSL_UTF8 = '1'
function Test-Wsl { $script:wslSays = (Tool wsl.exe @('-e', 'true')) -join ' '; $LASTEXITCODE -eq 0 }
$wsl = Test-Wsl
if (-not $wsl -and $wslSays -match 'wsl(\.exe)? --update' -and (Ask 'WSL needs an update first. Update it now? Windows asks for permission'))
{
    Start-Process wsl.exe -ArgumentList '--update' -Verb RunAs -Wait
    $wsl = Test-Wsl
}
if ($wsl) { Note 'WSL is ready: F5 and Ctrl+F5 on the Linux configuration run the game in it.' }
elseif ($wslSays -match 'no installed distributions')
{
    if (Ask 'WSL has no Linux installed yet. Install Ubuntu now? It asks you to pick a Linux user name and password')
    {
        Start-Process wsl.exe -ArgumentList '--install', '-d', 'Ubuntu' -NoNewWindow -Wait
        $wsl = Test-Wsl
        if ($wsl) { Note 'Ubuntu is ready: F5 and Ctrl+F5 on the Linux configuration run the game in it.' }
    }
}
else
{
    if ($wslSays) { Note "WSL says: $wslSays" }
    if (Ask 'Install WSL with Ubuntu now? Windows asks for permission and may need a restart')
    {
        Start-Process wsl.exe -ArgumentList '--install', '-d', 'Ubuntu' -Verb RunAs -Wait
        Note 'When it is done (after restarting, if Windows asks), open Ubuntu from the Start menu once to create your Linux user, then run this setup again.'
    }
}

if (-not (Test-Path "$Root\Libraries\SDL3\lib\Linux\x64\libSDL3.a"))
{
    if (-not $wsl) { Fail 'SDL for Linux is missing, and building it needs WSL: run this setup again once WSL works.' }
    Build-Sdl Linux
}

Clean-Up
Write-Host "`nDone: build the Linux configuration again." -ForegroundColor Green
