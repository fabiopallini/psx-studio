[CmdletBinding()]
param([string]$Msys2 = 'C:\msys64', [switch]$Test)
$ErrorActionPreference = 'Stop'
$bash = Join-Path $Msys2 'usr\bin\bash.exe'
if (!(Test-Path -LiteralPath $bash)) { throw 'MSYS2 UCRT64 is required; see ../README.md.' }
$previous = @{}
foreach ($name in @('MSYSTEM','CHERE_INVOKING','PSX_LIB_ROOT','PSX_LIB_TARGET')) { $previous[$name] = [Environment]::GetEnvironmentVariable($name,'Process') }
try {
    $env:MSYSTEM='UCRT64'; $env:CHERE_INVOKING='1'; $env:PSX_LIB_ROOT=$PSScriptRoot
    $env:PSX_LIB_TARGET=if ($Test) {'test'} else {'all'}
    & $bash --login ($PSScriptRoot.Replace('\','/')+'/tools/windows-build.sh')
    if ($LASTEXITCODE -ne 0) { throw "PSX Lib build failed ($LASTEXITCODE)." }
} finally { foreach ($name in $previous.Keys) { [Environment]::SetEnvironmentVariable($name,$previous[$name],'Process') } }
