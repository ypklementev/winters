<#
.SYNOPSIS
    Builds WinTers into dist/: the portable exe and/or the MSIX package for the Microsoft Store.

.DESCRIPTION
    Needs Visual Studio (Desktop development with C++) and a Windows 10/11 SDK. Both come with
    the VS installer. The script finds the newest SDK that has makeappx, makepri, signtool and
    C++/WinRT, builds with it and stops if there is none. It never downloads anything.

    The version is set in CMakeLists.txt only: project(WinTers VERSION X.Y.Z).

.PARAMETER Target
    exe  - dist/WinTers_<version>_x64.exe  (portable)
    msix - dist/WinTers_<version>_x64.msix (unsigned: this is what goes to the Store)
    all  - both (default)

.PARAMETER TestSign
    Additionally signs a copy of the MSIX with a self-signed test certificate
    (Subject = the Store publisher) -> dist/WinTers_<version>_x64_testsigned.msix,
    which can be installed locally with Add-AppxPackage.

.EXAMPLE
    .\build.ps1
    .\build.ps1 -Target exe
    .\build.ps1 -Target msix -TestSign
#>
[CmdletBinding()]
param(
    [ValidateSet('exe', 'msix', 'all')]
    [string]$Target = 'all',
    [switch]$TestSign
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Root = $PSScriptRoot
$BuildDir = Join-Path $Root 'build\msvc'
$Dist = Join-Path $Root 'dist'
# Must match Identity/Publisher in packaging/AppxManifest.xml.
$Publisher = 'CN=BEB569F2-AE9B-46F2-8C45-1FB02FB35E6E'

function Invoke-Tool([string]$Exe, [string[]]$Arguments) {
    Write-Host "> $(Split-Path $Exe -Leaf) $($Arguments -join ' ')" -ForegroundColor DarkGray
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$(Split-Path $Exe -Leaf) failed (exit code $LASTEXITCODE)" }
}

function Find-WindowsSdk {
    $keys = 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots',
            'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows Kits\Installed Roots'
    foreach ($key in $keys) {
        $props = Get-ItemProperty -Path $key -Name KitsRoot10 -ErrorAction SilentlyContinue
        if (-not $props) { continue }
        $kits = $props.KitsRoot10
        $bin = Join-Path $kits 'bin'
        if (-not (Test-Path $bin)) { continue }
        $versions = Get-ChildItem $bin -Directory |
            Where-Object { $_.Name -match '^10\.\d+\.\d+\.\d+$' } |
            Sort-Object { [version]$_.Name } -Descending
        foreach ($v in $versions) {
            $x64 = Join-Path $v.FullName 'x64'
            $sdk = [pscustomobject]@{
                Version  = $v.Name
                MakeAppx = Join-Path $x64 'makeappx.exe'
                MakePri  = Join-Path $x64 'makepri.exe'
                SignTool = Join-Path $x64 'signtool.exe'
                CppWinRT = Join-Path $kits "Include\$($v.Name)\cppwinrt\winrt\Windows.ApplicationModel.h"
            }
            if ((Test-Path $sdk.MakeAppx) -and (Test-Path $sdk.MakePri) -and
                (Test-Path $sdk.SignTool) -and (Test-Path $sdk.CppWinRT)) {
                return $sdk
            }
        }
    }
    return $null
}

function Find-CMake {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    # Not on PATH (plain PowerShell instead of Developer PowerShell): use the one bundled with VS.
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vs) {
            $bundled = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
            if (Test-Path $bundled) { return $bundled }
        }
    }
    throw 'CMake not found. Install Visual Studio with "Desktop development with C++" (it includes CMake).'
}

# ---------------------------------------------------------------------------
# Toolchain
# ---------------------------------------------------------------------------
$sdk = Find-WindowsSdk
if (-not $sdk) {
    throw ('Windows 10/11 SDK not found (need makeappx.exe, makepri.exe, signtool.exe and C++/WinRT headers). ' +
           'Install it with the Visual Studio Installer: Individual components -> "Windows 11 SDK".')
}
Write-Host "Windows SDK $($sdk.Version)"
$cmake = Find-CMake

# ---------------------------------------------------------------------------
# Build (Release, x64). CMAKE_SYSTEM_VERSION pins the same SDK, which also becomes
# MaxVersionTested in the MSIX manifest.
# ---------------------------------------------------------------------------
Invoke-Tool $cmake @('-S', $Root, '-B', $BuildDir, '-A', 'x64', "-DCMAKE_SYSTEM_VERSION=$($sdk.Version)")
Invoke-Tool $cmake @('--build', $BuildDir, '--config', 'Release', '--parallel')

$exe = Join-Path $BuildDir 'Release\WinTers.exe'
$version = (Get-Content (Join-Path $BuildDir 'version.txt') -Raw).Trim()
New-Item -ItemType Directory -Force $Dist | Out-Null
$outputs = @()

if ($Target -in 'exe', 'all') {
    $portable = Join-Path $Dist "WinTers_${version}_x64.exe"
    Copy-Item $exe $portable -Force
    $outputs += $portable
}

if ($Target -in 'msix', 'all') {
    # Layout: exactly what goes into the package.
    $msixDir = Join-Path $BuildDir 'msix'
    $layout = Join-Path $msixDir 'layout'
    if (Test-Path $layout) { Remove-Item $layout -Recurse -Force }
    New-Item -ItemType Directory -Force $layout | Out-Null
    Copy-Item $exe (Join-Path $layout 'WinTers.exe')
    Copy-Item (Join-Path $msixDir 'AppxManifest.xml') $layout  # generated by CMake
    Copy-Item (Join-Path $Root 'packaging\Assets') (Join-Path $layout 'Assets') -Recurse

    # resources.pri: lets Windows pick the scale-/targetsize- logo variants.
    $priConfig = Join-Path $msixDir 'priconfig.xml'
    Invoke-Tool $sdk.MakePri @('createconfig', '/cf', $priConfig, '/dq', 'ru-RU_en-US', '/pv', '10.0.0', '/o')
    Invoke-Tool $sdk.MakePri @('new', '/pr', $layout, '/cf', $priConfig,
        '/mn', (Join-Path $layout 'AppxManifest.xml'), '/of', (Join-Path $layout 'resources.pri'), '/o')

    $msix = Join-Path $Dist "WinTers_${version}_x64.msix"
    Invoke-Tool $sdk.MakeAppx @('pack', '/d', $layout, '/p', $msix, '/o')
    $outputs += $msix

    if ($TestSign) {
        $cert = Get-ChildItem Cert:\CurrentUser\My |
            Where-Object { $_.Subject -eq $Publisher -and $_.HasPrivateKey -and $_.NotAfter -gt (Get-Date) } |
            Sort-Object NotAfter -Descending | Select-Object -First 1
        if (-not $cert) {
            Write-Host "Creating test certificate $Publisher in Cert:\CurrentUser\My"
            $cert = New-SelfSignedCertificate -Type Custom -Subject $Publisher `
                -KeyUsage DigitalSignature -FriendlyName 'WinTers test signing' `
                -CertStoreLocation Cert:\CurrentUser\My `
                -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')
        }
        $cer = Join-Path $Dist 'WinTers_TestCert.cer'
        Export-Certificate -Cert $cert -FilePath $cer | Out-Null

        # Sign a copy: the unsigned .msix stays as the Store upload.
        $signed = Join-Path $Dist "WinTers_${version}_x64_testsigned.msix"
        Copy-Item $msix $signed -Force
        Invoke-Tool $sdk.SignTool @('sign', '/fd', 'SHA256', '/s', 'My', '/sha1', $cert.Thumbprint, $signed)
        $outputs += $signed, $cer

        Write-Host ''
        Write-Host 'To install the test-signed package, trust the certificate once (elevated PowerShell):' -ForegroundColor Yellow
        Write-Host "  Import-Certificate -FilePath '$cer' -CertStoreLocation Cert:\LocalMachine\TrustedPeople"
        Write-Host 'then (normal PowerShell):' -ForegroundColor Yellow
        Write-Host "  Add-AppxPackage -Path '$signed'"
    }
}

Write-Host ''
Write-Host "WinTers $version" -ForegroundColor Green
$outputs | ForEach-Object { Write-Host "  $_" }
