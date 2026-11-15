param(
  [string]$Profile = (Join-Path $env:APPDATA 'obs-studio\plugin_config\mk8dx-quickscore'),
  [string]$Runtime = (Join-Path $env:LOCALAPPDATA 'MK8DXQuickScore\VisualJev'),
  [string]$Python = ''
)
$ErrorActionPreference = 'Stop'
$existing = Join-Path $Profile 'local-runtime.json'
if (Test-Path -LiteralPath $existing) {
  $config = Get-Content -LiteralPath $existing -Raw | ConvertFrom-Json
  if ((Test-Path -LiteralPath $config.pythonPath) -and
      (Test-Path -LiteralPath (Join-Path $config.modelDirectory 'model.safetensors.index.json')) -and
      (Test-Path -LiteralPath (Join-Path $config.adapterDirectory 'adapter_model.safetensors'))) {
    Write-Output 'QUICKSCORE_LOCAL_READY'
    exit 0
  }
}
if (-not $Python) {
  $bundled = Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
  if (Test-Path -LiteralPath $bundled) { $Python = $bundled }
  else {
    $command = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($command -and $command.Source -notlike '*WindowsApps*') { $Python = $command.Source }
  }
}
if (-not $Python -or -not (Test-Path -LiteralPath $Python)) {
  throw 'Python 3.12 or 3.13 is required for first-time setup. Install it from python.org and retry.'
}
& $Python -c "import sys; assert (3, 12) <= sys.version_info[:2] <= (3, 13), 'Use Python 3.12 or 3.13'"
if ($LASTEXITCODE -ne 0) { throw 'Unsupported Python version' }
New-Item -ItemType Directory -Path $Runtime -Force | Out-Null
$runtimePython = Join-Path $Runtime 'venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $runtimePython)) {
  & $Python -m venv (Join-Path $Runtime 'venv')
  if ($LASTEXITCODE -ne 0) { throw 'Runtime creation failed' }
}
& $runtimePython -m pip install 'torch==2.11.0' 'torchvision==0.26.0' --index-url https://download.pytorch.org/whl/cu128
if ($LASTEXITCODE -ne 0) { throw 'CUDA runtime installation failed' }
& $runtimePython -m pip install -r (Join-Path $PSScriptRoot 'requirements.txt')
if ($LASTEXITCODE -ne 0) { throw 'Inference dependencies installation failed' }
& $runtimePython -c "import torch; assert torch.cuda.is_available(), 'Compatible NVIDIA GPU and driver required'"
if ($LASTEXITCODE -ne 0) { throw 'CUDA GPU is unavailable' }
& $runtimePython (Join-Path $PSScriptRoot 'setup-models.py') --runtime $Runtime --profile $Profile
if ($LASTEXITCODE -ne 0) { throw 'Model verification failed' }
