[CmdletBinding()]
param([string]$Prefix)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$Prefix) {
    $compiler = (Get-Command gcc.exe -ErrorAction Stop).Source
    $Prefix = Split-Path (Split-Path $compiler -Parent) -Parent
}
$Prefix = [IO.Path]::GetFullPath($Prefix)
$objdump = Join-Path $Prefix 'bin\objdump.exe'
foreach ($library in @('libEGL.dll', 'libGLESv2.dll')) {
    if (!(Test-Path -LiteralPath (Join-Path $Prefix ('bin\' + $library)))) {
        throw "Missing $library. Install: pacman -S mingw-w64-ucrt-x86_64-angleproject"
    }
}
$base = [IO.Path]::GetFullPath((Join-Path $root 'build\windows'))
$output = Join-Path $base 'psx-studio'
$stage = Join-Path $base '.psx-studio-stage'
$marker = '.psx-studio-package'
function Assert-ManagedDirectory([string]$Directory) {
    $resolved = [IO.Path]::GetFullPath($Directory)
    if ((Split-Path $resolved -Parent) -ne $base -or (Split-Path $resolved -Leaf) -notin @('psx-studio', '.psx-studio-stage')) {
        throw "Unexpected package path: $resolved"
    }
    if (Test-Path -LiteralPath $resolved) {
        if ((Get-Item -LiteralPath $resolved -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Package directory cannot be a link: $resolved"
        }
        if (!(Test-Path -LiteralPath (Join-Path $resolved $marker) -PathType Leaf)) {
            throw "Refusing to replace an unmanaged directory: $resolved"
        }
    }
}
function Copy-Tree([string]$Source, [string]$Destination) {
    if (Test-Path -LiteralPath $Source -PathType Container) {
        [IO.Directory]::CreateDirectory((Split-Path $Destination -Parent)) | Out-Null
        Copy-Item -LiteralPath $Source -Destination $Destination -Recurse
    }
}
$copied = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$imports = @{}
function Copy-Binary([string]$Binary, [switch]$ScanOnly) {
    $key = [IO.Path]::GetFullPath($Binary)
    if (!$copied.Add($key)) { return }
    if (!$ScanOnly) { Copy-Item -LiteralPath $Binary -Destination (Join-Path $stage (Split-Path $Binary -Leaf)) -Force }
    if (!$imports.ContainsKey($key)) {
        $listing = & $objdump -p $Binary
        if ($LASTEXITCODE -ne 0) { throw "Cannot inspect DLL dependencies: $Binary" }
        $imports[$key] = @($listing | Select-String 'DLL Name:\s*(\S+)' | ForEach-Object { $_.Matches[0].Groups[1].Value })
    }
    foreach ($name in $imports[$key]) {
        $library = Join-Path $Prefix ('bin\' + $name)
        if (Test-Path -LiteralPath $library -PathType Leaf) { Copy-Binary $library }
        elseif ($name -notmatch '^(api-ms-|ext-ms-)' -and !(Test-Path -LiteralPath (Join-Path $env:SystemRoot ('System32\' + $name)))) {
            throw "Missing dependency $name required by $Binary"
        }
    }
}
foreach ($directory in @($stage, $output)) { Assert-ManagedDirectory $directory }
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
[IO.Directory]::CreateDirectory($stage) | Out-Null
[IO.File]::WriteAllText((Join-Path $stage $marker), "PSX Studio Windows distribution`n")
Write-Host 'Packaging Windows editor, DLLs and GTK resources (no Python)...'
Copy-Binary (Join-Path $root 'build\psx-studio.exe')
# EGL and GLES are loaded dynamically by GTK/Epoxy, outside the EXE import table.
Copy-Binary (Join-Path $Prefix 'bin\libEGL.dll')
Copy-Binary (Join-Path $Prefix 'bin\libGLESv2.dll')
foreach ($relative in @('lib\gdk-pixbuf-2.0', 'lib\gio\modules', 'etc\fonts',
        'share\glib-2.0\schemas', 'share\icons', 'share\gtk-4.0', 'share\licenses')) {
    Copy-Tree (Join-Path $Prefix $relative) (Join-Path $stage $relative)
}
# Dynamically loaded image plugins also have their own DLL dependencies.
Get-ChildItem -LiteralPath (Join-Path $stage 'lib') -Filter '*.dll' -File -Recurse | ForEach-Object { Copy-Binary $_.FullName -ScanOnly }
$locale = Join-Path $Prefix 'share\locale'
Get-ChildItem -LiteralPath $locale -Filter '*.mo' -File -Recurse | Where-Object { $_.Name -in @('gtk40.mo', 'glib20.mo', 'gdk-pixbuf.mo', 'json-glib-1.0.mo') } | ForEach-Object {
    $relative = $_.FullName.Substring($locale.Length).TrimStart('\')
    $destination = Join-Path (Join-Path $stage 'share\locale') $relative
    [IO.Directory]::CreateDirectory((Split-Path $destination -Parent)) | Out-Null
    Copy-Item -LiteralPath $_.FullName -Destination $destination
}
foreach ($relative in @('pipeline', 'runtime', 'docs')) { Copy-Tree (Join-Path $root $relative) (Join-Path $stage $relative) }
Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination (Join-Path $stage 'README.md')
# Only clean generated package content, after resolving and checking both paths.
Get-ChildItem -LiteralPath $stage -Filter '*.a' -Recurse -File | Remove-Item -Force
$text = @"
Run psx-studio.exe directly. Keep every file and folder together.
MSYS2 and Python are not required on the recipient's computer.
Building PS1 games requires your own Psy-Q SDK and DOSBox/DOSBox-X.
Configure their Windows paths in the editor settings.
Third-party library notices are in share/licenses.
"@
[IO.File]::WriteAllText((Join-Path $stage 'START-HERE.txt'), $text)
# Check loading with MSYS2 removed from PATH; this opens no GUI or game.
$previousPath = $env:PATH
try {
    $env:PATH = Join-Path $env:SystemRoot 'System32'
    Copy-Item -LiteralPath (Join-Path $root 'build\windows\check-gl.exe') -Destination (Join-Path $stage 'check-gl.exe')
    try {
        & (Join-Path $stage 'check-gl.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Packaged EGL/ANGLE graphics check failed.' }
    } finally { Remove-Item -LiteralPath (Join-Path $stage 'check-gl.exe') -Force }
    & (Join-Path $stage 'psx-studio.exe') --help | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Packaged EXE failed its load check: $LASTEXITCODE" }
    & (Join-Path $stage 'psx-studio.exe') --build-ps1 --help | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Native build command failed its load check: $LASTEXITCODE" }
} finally { $env:PATH = $previousPath }
Assert-ManagedDirectory $output
if (Test-Path -LiteralPath $output) { Remove-Item -LiteralPath $output -Recurse -Force }
Move-Item -LiteralPath $stage -Destination $output
Write-Host "Ready to ZIP: $output"
