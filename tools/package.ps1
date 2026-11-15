param(
  [string]$BuildDir = 'build',
  [string]$OutputDir = 'dist',
  [string]$TlsBackend = '',
  [string]$CMake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
function Resolve-ProjectPath([string]$path) {
  if ([IO.Path]::IsPathRooted($path)) { return [IO.Path]::GetFullPath($path) }
  return [IO.Path]::GetFullPath((Join-Path $projectRoot $path))
}
$build = Resolve-ProjectPath $BuildDir
$output = Resolve-ProjectPath $OutputDir
$version = (Get-Content -LiteralPath (Join-Path $projectRoot 'buildspec.json') -Raw | ConvertFrom-Json).version
$package = Join-Path $output ('QuickScore-Jev-API-' + $version)
if (Test-Path -LiteralPath $package) { throw 'Package directory exists. Choose a fresh OutputDir; no files were removed.' }
Push-Location $projectRoot
try {
  $status = @(& git status --porcelain)
  if ($LASTEXITCODE -ne 0 -or $status.Count -gt 0) { throw 'Commit the source before packaging.' }
  $commit = (& git rev-parse HEAD).Trim()
  $files = @(& git -c core.quotepath=false ls-files)
  if ($LASTEXITCODE -ne 0) { throw 'Cannot enumerate committed source.' }
  if ($TlsBackend) {
    & $CMake -S $projectRoot -B $build "-DQUICKSCORE_TLS_BACKEND=$([IO.Path]::GetFullPath($TlsBackend))"
    if ($LASTEXITCODE -ne 0) { throw 'TLS configuration failed.' }
  }
  New-Item -ItemType Directory -Path $package -Force | Out-Null
  & $CMake --install $build --prefix (Join-Path $package 'mk8dx-quickscore')
  if ($LASTEXITCODE -ne 0) { throw 'CMake install failed.' }
  foreach ($name in @('README.md','INSTALL.txt','LICENSE','CHANGELOG.md','support.ps1','install.bat')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $name) -Destination $package
  }
  foreach ($name in @('docs','third-party')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $name) -Destination $package -Recurse
  }
  $sourceRoot = Join-Path $package 'source'
  foreach ($name in $files) {
    $source = Join-Path $projectRoot $name
    $destination = [IO.Path]::GetFullPath((Join-Path $sourceRoot $name))
    if (-not $destination.StartsWith($sourceRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid source path.' }
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($destination)) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination
  }
  $builtDll = Join-Path $build 'mk8dx-quickscore.dll'
  $packagedDll = Join-Path $package 'mk8dx-quickscore\bin\64bit\mk8dx-quickscore.dll'
  if ((Get-FileHash -LiteralPath $builtDll).Hash -ne (Get-FileHash -LiteralPath $packagedDll).Hash) { throw 'DLL hash mismatch.' }
  $hashes = [ordered]@{}
  Get-ChildItem -LiteralPath $package -File -Recurse | ForEach-Object {
    $relative = $_.FullName.Substring($package.Length + 1).Replace('\','/')
    $hashes[$relative] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
  }
  [ordered]@{version=$version; sourceCommit=$commit; files=$hashes} | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath (Join-Path $package 'manifest.json') -Encoding utf8
  $zip = $package + '.zip'
  Compress-Archive -Path (Join-Path $package '*') -DestinationPath $zip
  Write-Output ([pscustomobject]@{zip=$zip;sourceCommit=$commit;sha256=(Get-FileHash -LiteralPath $zip).Hash} | ConvertTo-Json)
} finally { Pop-Location }
