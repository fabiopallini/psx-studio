[CmdletBinding()]
param(
    [ValidateSet('build', 'run', 'test', 'clean')]
    [string]$Target = 'build',
    [string]$Msys2 = 'C:\msys64',
    [string]$Project,
    [string]$Psyq
)
$ErrorActionPreference = 'Stop'
$bash = Join-Path $Msys2 'usr\bin\bash.exe'
if (!(Test-Path -LiteralPath $bash)) {
    throw "MSYS2 non trovato in $Msys2. Installa MSYS2 oppure usa -Msys2 PERCORSO. Vedi README.md."
}
$names = 'MSYSTEM', 'CHERE_INVOKING', 'PSX_STUDIO_ROOT', 'PSX_STUDIO_TARGET', 'PSX_STUDIO_PROJECT', 'PSX_STUDIO_SDK'
$previous = @{}
foreach ($name in $names) { $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    $env:MSYSTEM = 'UCRT64'
    $env:CHERE_INVOKING = '1'
    $env:PSX_STUDIO_ROOT = $PSScriptRoot
    $env:PSX_STUDIO_TARGET = $Target
    $env:PSX_STUDIO_PROJECT = $Project
    $env:PSX_STUDIO_SDK = $Psyq
    # Keep user paths in environment variables, outside shell source text.
    & $bash --login ($PSScriptRoot.Replace('\', '/') + '/tools/windows-build.sh')
    if ($LASTEXITCODE -ne 0) { throw "Compilazione fallita (codice $LASTEXITCODE)." }
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process') }
}