$ErrorActionPreference = 'Stop'
if (Get-Process -Name obs64 -ErrorAction SilentlyContinue) {
  throw 'OBSを終了してからインストールしてください。'
}
$packageRoot = Join-Path $PSScriptRoot 'mk8dx-quickscore'
$installedRoot = Join-Path $env:ProgramData 'obs-studio\plugins\mk8dx-quickscore'
$backupRoot = Join-Path $env:ProgramData ('OBS-Plugin-Backups\mk8dx-quickscore\' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$sourceDll = Join-Path $packageRoot 'bin\64bit\mk8dx-quickscore.dll'
if (-not (Test-Path -LiteralPath $sourceDll)) { throw '同梱DLLがありません。ZIPをすべて展開してください。' }
if (Test-Path -LiteralPath $installedRoot) {
  New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null
  Get-ChildItem -LiteralPath $installedRoot -Force | Copy-Item -Destination $backupRoot -Recurse -Force
  Write-Host "既存版バックアップ: $backupRoot"
}
New-Item -ItemType Directory -Path $installedRoot -Force | Out-Null
Get-ChildItem -LiteralPath $packageRoot -Force | Copy-Item -Destination $installedRoot -Recurse -Force
$installedDll = Join-Path $installedRoot 'bin\64bit\mk8dx-quickscore.dll'
if ((Get-FileHash -LiteralPath $sourceDll -Algorithm SHA256).Hash -ne
    (Get-FileHash -LiteralPath $installedDll -Algorithm SHA256).Hash) { throw 'コピー後のDLLハッシュが一致しません。' }
Write-Host 'コピー完了。OBSを起動し「ドック → MK8DX QuickScore」を開いてください。'
Write-Host 'Jev APIのキーをドックで設定してください。ローカルモデルの準備は不要です。'
