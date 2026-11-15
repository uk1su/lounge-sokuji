param(
  [Parameter(Mandatory=$true)][string]$DepsRoot,
  [Parameter(Mandatory=$true)][string]$QtRoot,
  [string]$CppWinrt = '',
  [string]$BuildDir = (Join-Path $PSScriptRoot 'build'),
  [string]$VsRoot = 'C:\Program Files\Microsoft Visual Studio\2022\Community',
  [switch]$ConfigureOnly,
  [switch]$Clean
)
$ErrorActionPreference = 'Stop'
$env:VSLANG = '1033'
$compilerRoot = Get-ChildItem -LiteralPath (Join-Path $VsRoot 'VC\Tools\MSVC') -Directory |
  Sort-Object Name -Descending | Select-Object -First 1
$kitsRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$sdkVersion = Get-ChildItem -LiteralPath (Join-Path $kitsRoot 'Include') -Directory |
  Sort-Object Name -Descending | Select-Object -First 1
if (-not $compilerRoot -or -not $sdkVersion) { throw 'MSVC or Windows SDK missing' }
$cmake = Join-Path $VsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninjaDir = Join-Path $VsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
$compilerBin = Join-Path $compilerRoot.FullName 'bin\Hostx64\x64'
$sdkInclude = Join-Path $kitsRoot ('Include\' + $sdkVersion.Name)
$sdkLib = Join-Path $kitsRoot ('Lib\' + $sdkVersion.Name)
$env:INCLUDE = (@((Join-Path $compilerRoot.FullName 'include')) +
  @('ucrt','shared','um','winrt','cppwinrt' | ForEach-Object { Join-Path $sdkInclude $_ })) -join ';'
$env:LIB = (@((Join-Path $compilerRoot.FullName 'lib\x64')) +
  @('ucrt\x64','um\x64' | ForEach-Object { Join-Path $sdkLib $_ })) -join ';'
$env:PATH = $compilerBin + ';' + $ninjaDir + ';' + (Join-Path $kitsRoot ('bin\' + $sdkVersion.Name + '\x64')) + ';' + $env:PATH
& $cmake -S $PSScriptRoot -B $BuildDir -G Ninja '-DCMAKE_BUILD_TYPE=RelWithDebInfo' `
  "-DQUICKSCORE_DEPENDENCIES=$DepsRoot" "-DQt6_DIR=$QtRoot/lib/cmake/Qt6" `
  "-DCPPWINRT_GENERATED=$CppWinrt"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
if (-not $ConfigureOnly) {
  if ($Clean) { & $cmake --build $BuildDir --clean-first --parallel 4 }
  else { & $cmake --build $BuildDir --parallel 4 }
  if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
}
