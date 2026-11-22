# Android's PowerShell side, which Build.bat runs:
#   Setup.ps1             installs Java, the Android SDK and NDK, and SDL for Android
#   Setup.ps1 -Keystore   makes Keystore\Keystore.keystore, the release key Google Play knows the game by, and
#                         Keystore\Keystore.bat, which Build.bat reads it with. Back the Keystore folder up.
#   Setup.ps1 -Package <archive> -Stage <folder> [-Bundle]
#                         adds classes.dex to aapt2's .apk (Build.bat adds lib\), or with -Bundle turns its
#                         --proto-format output into a module for bundletool (manifest\, dex\, lib\)
param([switch] $Keystore, [string] $Package, [string] $Stage, [switch] $Bundle)

if ($Package)
{
    $ErrorActionPreference = 'Stop'
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem

    $root = (Get-Item -LiteralPath $Stage).FullName.TrimEnd('\', '/')
    $zip = [System.IO.Compression.ZipFile]::Open($Package, [System.IO.Compression.ZipArchiveMode]::Update)
    try
    {
        function Add-File([string] $Path, [string] $Name)
        {
            $existing = $zip.GetEntry($Name)
            if ($existing) { $existing.Delete() }
            [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $Path, $Name, [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }

        if ($Bundle)
        {
            $manifest = $zip.GetEntry('AndroidManifest.xml')
            $moved = $zip.CreateEntry('manifest/AndroidManifest.xml')
            $from = $manifest.Open(); $to = $moved.Open()
            $from.CopyTo($to)
            $from.Dispose(); $to.Dispose()
            $manifest.Delete()
        }

        Add-File (Join-Path $root 'classes.dex') ($(if ($Bundle) { 'dex/classes.dex' } else { 'classes.dex' }))
        # The bundle's libraries only: Build.bat stores the .apk's uncompressed, which this zip can't
        foreach ($library in Get-ChildItem -LiteralPath (Join-Path $root 'lib') -Recurse -File -Filter '*.so' | Where-Object { $Bundle })
        {
            Add-File $library.FullName $library.FullName.Substring($root.Length + 1).Replace('\', '/')
        }
    }
    finally
    {
        $zip.Dispose()
    }
    exit 0
}

. "$PSScriptRoot\..\..\SetupHelpers.ps1"

function New-Keystore
{
    $folder = Join-Path $Root 'Keystore'
    if (Test-Path "$folder\Keystore.keystore") { Fail "$folder\Keystore.keystore already exists. Delete it first to make a new one (published games must keep their key)." }

    $java = @("$Tools\Android\jdk") | Where-Object { Test-Path "$_\bin\keytool.exe" }
    if (-not $java) { Fail 'Java was not found: run the Android setup first.' }

    Step 'Release key'
    Note 'Batch files cannot carry some characters, so the password can use letters, digits and - _ . @ # $ + = only.'
    do
    {
        $first = [Net.NetworkCredential]::new('', (Read-Host '   Password (6 or more characters)' -AsSecureString)).Password
        $second = [Net.NetworkCredential]::new('', (Read-Host '   The same password again' -AsSecureString)).Password
        $valid = $first -eq $second -and $first.Length -ge 6 -and $first -match '^[A-Za-z0-9\-_.@#$+=]+$'
        if (-not $valid) { Note 'The passwords differ, are too short or use other characters: try again.' }
    } until ($valid)
    $name = Read-Host '   Your name or studio, for the certificate'
    if (-not $name) { $name = 'Unknown' }

    New-Item -ItemType Directory -Force $folder | Out-Null
    Tool "$java\bin\keytool.exe" @('-genkeypair', '-keystore', "$folder\Keystore.keystore", '-alias', 'release', '-keyalg', 'RSA', '-keysize', '2048', '-validity', '10000',
        '-storepass', $first, '-keypass', $first, '-dname', "CN=$($name -replace '[,=+<>#;"\\]', ' ')") | Out-Host
    if ($LASTEXITCODE) { Fail 'keytool could not make the key.' }

    [IO.File]::WriteAllLines("$folder\Keystore.bat", [string[]] @(
        'rem | The Android release key, read by Framework\Automations\Platforms\Android\Build.bat. Keep this folder private.'
        'set "ANDROID_KEYSTORE=%~dp0Keystore.keystore"'
        'set "ANDROID_KEY_ALIAS=release"'
        "set `"ANDROID_KEYSTORE_PASSWORD=$first`""))
    [IO.File]::WriteAllLines("$folder\Keystore.txt", [string[]] @(
        "Keystore:          Keystore.keystore"
        "Alias:             release"
        "Password:          $first"
        "Name or studio:    $name"
        "Made:              $(Get-Date -Format 'yyyy-MM-dd')"))

    Note "Made $folder\Keystore.keystore. Android builds now sign with it and also make the .aab for Google Play."
    Write-Host "   Back up the Keystore folder (git leaves it out): every update on Google Play must be signed with this same key." -ForegroundColor Yellow
}

if ($Keystore) { New-Keystore; Clean-Up; exit 0 }

Write-Host "Android setup: Java, the Android SDK and NDK (about 4 GB, no Android Studio needed), DXC for the shaders, then SDL built for Android."

$android = Join-Path $Tools 'Android'
$sdk = Join-Path $android 'Sdk'
$env:ANDROID_AVD_HOME = Join-Path $android 'user\avd'
$env:ANDROID_USER_HOME = Join-Path $android 'user'

Step 'Java'
$jdk = @(Join-Path $android 'jdk') | Where-Object { Test-Path "$_\bin\javac.exe" }
if (-not $jdk)
{
    $archive = Join-Path $Temporary 'jdk.zip'
    Download 'https://aka.ms/download-jdk/microsoft-jdk-21-windows-x64.zip' $archive
    $jdk = Join-Path $android 'jdk'
    Unpack $archive $jdk (Only-Folder $archive)
}
Note "Java is in $jdk"
$env:JAVA_HOME = $jdk
$env:Path = "$jdk\bin;$env:Path"

Step 'Android SDK command-line tools'
# Google's list of SDK packages, read by element name only
$repository = [xml](Invoke-WebRequest 'https://dl.google.com/android/repository/repository2-3.xml' -UseBasicParsing).Content
function Child($Node, [string] $Name) { if ($Node) { $Node.ChildNodes | Where-Object { $_.LocalName -eq $Name } | Select-Object -First 1 } }
function Is-Stable($Package) { $channel = Child $Package 'channelRef'; $channel -and $channel.GetAttribute('ref') -eq 'channel-0' -and $Package.GetAttribute('obsolete') -ne 'true' }
$remote = @($repository.DocumentElement.ChildNodes | Where-Object { $_.LocalName -eq 'remotePackage' })
$stable = @($remote | Where-Object { Is-Stable $_ } | ForEach-Object { $_.GetAttribute('path') })
function Newest([string] $Pattern)
{
    $stable | Where-Object { $_ -match "^$Pattern$" } | Sort-Object { [version](($_ -replace '^[^;]+;(android-)?', '') + '.0') } | Select-Object -Last 1
}

$sdkManager = Join-Path $sdk 'cmdline-tools\latest\bin\sdkmanager.bat'
if (-not (Test-Path $sdkManager))
{
    $file = $null
    $commandLine = @($remote | Where-Object { $_.GetAttribute('path') -eq 'cmdline-tools;latest' }) + @($remote | Where-Object { $_.GetAttribute('path') -eq (Newest 'cmdline-tools;[\d.]+') })
    foreach ($archive in $commandLine | ForEach-Object { (Child $_ 'archives').ChildNodes })
    {
        if (-not $file -and (Child $archive 'host-os').InnerText -eq 'windows') { $file = 'https://dl.google.com/android/repository/' + (Child (Child $archive 'complete') 'url').InnerText }
    }
    if (-not $file)
    {
        # The download page links the same zip
        $page = (Invoke-WebRequest 'https://developer.android.com/studio' -UseBasicParsing).Content
        $file = [regex]::Match($page, 'https://dl\.google\.com/android/repository/commandlinetools-win-\d+_latest\.zip').Value
    }
    if (-not $file) { Fail "Could not find the Android command-line tools. Get `"Command line tools only`" for Windows from developer.android.com/studio, unpack its cmdline-tools folder to $sdk\cmdline-tools\latest, then run this setup again." }
    $archive = Join-Path $Temporary 'cmdline-tools.zip'
    Download $file $archive
    Unpack $archive (Join-Path $sdk 'cmdline-tools\latest') 'cmdline-tools'
}
Note "The Android SDK is in $sdk"

Step 'Android SDK packages: platform, build tools, NDK, CMake'
# sdkmanager's notice that the Android CLI replaces it (it still works), hidden
$notice = 'deprecated|Android CLI|android sdk|d\.android\.com/tools/agents|--licenses option is no longer needed'
Tool $sdkManager @("--sdk_root=$sdk", '--licenses') (1..30 | ForEach-Object { 'y' }) | Out-Null
# Package names go in a file: sdkmanager.bat splits arguments at the ; in platforms;android-36
function Install-Packages([string[]] $Names)
{
    $list = Join-Path $Temporary 'Packages.txt'
    [IO.File]::WriteAllLines($list, $Names)
    Tool $sdkManager @("--sdk_root=$sdk", "--package_file=$list") | Where-Object { $_ -notmatch $notice } | Out-Host
}
$least = Get-Version 'NDK_VERSION'
$ndks = @(Get-ChildItem (Join-Path $sdk 'ndk') -Directory -ErrorAction SilentlyContinue | ForEach-Object Name)
$ndk = if (-not $least) { Newest 'ndk;\d+\.\d+\.\d+' } elseif (-not ($ndks | Where-Object { -not (Is-Older $_ $least) })) { "ndk;$least" }
$packages = @('platform-tools', (Newest 'platforms;android-\d+'), (Newest 'build-tools;\d+\.\d+\.\d+'), $ndk, (Newest 'cmake;\d+\.\d+\.\d+')) | Where-Object { $_ }
Install-Packages $packages
if ($LASTEXITCODE -or -not ((Test-Path "$sdk\platforms\*") -and (Test-Path "$sdk\build-tools\*") -and (Test-Path "$sdk\ndk\*") -and (Test-Path "$sdk\cmake\*")))
{
    Fail 'The Android SDK packages could not be installed: see the messages above.'
}

Step 'bundletool, which makes the .aab for Google Play'
$bundletool = Join-Path $sdk 'bundletool\bundletool.jar'
if (-not (Test-Path $bundletool))
{
    $release = Invoke-RestMethod 'https://api.github.com/repos/google/bundletool/releases/latest'
    Download ($release.assets | Where-Object { $_.name -like 'bundletool-all-*.jar' } | Select-Object -First 1).browser_download_url $bundletool
}
Note "bundletool is in $bundletool"

Install-Dxc

$missing = (Get-Content "$PSScriptRoot\Config.bat" | Select-String 'ABIS=([^"]*)').Matches.Groups[1].Value -split '\s+' |
    Where-Object { $_ -and -not (Test-Path "$Root\Libraries\SDL3\lib\Android\$_\libSDL3.so") }
if ($missing -or -not (Test-Path "$Root\Libraries\SDL3\lib\Android\java")) { Build-Sdl Android }

Step 'An emulator, to test without a phone (optional, about 4 GB more)'
$avds = if (Test-Path "$sdk\emulator\emulator.exe") { Tool "$sdk\emulator\emulator.exe" @('-list-avds') | Where-Object { $_ -match '^[\w.-]+$' } } else { @() }
if ($avds) { Note "Found the emulator $($avds | Select-Object -First 1)" }
elseif (Ask 'Install an Android emulator?')
{
    $api = ($packages[1] -split '-')[-1]
    $image = "system-images;android-$api;google_apis;x86_64"
    Install-Packages @('emulator', $image)
    Tool (Join-Path $sdk 'cmdline-tools\latest\bin\avdmanager.bat') @('create', 'avd', '--name', 'Phone', '--package', $image, '--device', 'pixel_7', '--force') @('no') | Out-Null
    if ($LASTEXITCODE) { Fail 'The emulator could not be made: see the messages above.' }
    Note 'Made the emulator "Phone": F5 and Ctrl+F5 on the Android configuration start it when no phone is connected.'
}

Step 'A release key, to publish on Google Play (optional: "Build.bat keystore" in this folder makes it later)'
if (Test-Path "$Root\Keystore\Keystore.bat") { Note 'Found Keystore\Keystore.bat' }
elseif (Ask 'Make your release key now?') { New-Keystore }

Clean-Up
Write-Host "`nDone: build the Android configuration again." -ForegroundColor Green
