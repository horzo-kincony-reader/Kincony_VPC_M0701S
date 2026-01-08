param(
  [string]$RepoPath    = "D:\KINCONY\ARDIUNO\GithubCopilot\REPOSITORY\Kincony_VPC_M0701S",
  [string]$Branch      = "feature/vpc-m0701s",
  [string]$RemoteOwner = "horzo-kincony-reader",
  [string]$RepoName    = "Kincony_VPC_M0701S",
  [switch]$FixRemote   # Użyj, aby automatycznie ustawić remote origin na prawidłowy URL, jeśli mismatch
)

Write-Host "== Backup precompile =="

# Przejście do katalogu repozytorium
if (-not (Test-Path $RepoPath)) {
  throw "RepoPath not found: $RepoPath"
}
Set-Location -Path $RepoPath

# Weryfikacja remote origin
$expectedUrl = "https://github.com/$RemoteOwner/$RepoName.git"
try {
  $originUrl = git remote get-url origin 2>$null
} catch { $originUrl = "" }

if ([string]::IsNullOrWhiteSpace($originUrl)) {
  Write-Warning "No 'origin' remote configured. Expected: $expectedUrl"
  if ($FixRemote) {
    Write-Host "Setting origin to $expectedUrl..."
    git remote add origin $expectedUrl
  }
} elseif ($originUrl -ne $expectedUrl) {
  Write-Warning "origin remote mismatch. Current: $originUrl; Expected: $expectedUrl"
  if ($FixRemote) {
    Write-Host "Updating origin to $expectedUrl..."
    git remote set-url origin $expectedUrl
  } else {
    Write-Host "Tip: run with -FixRemote to update the remote URL automatically."
  }
} else {
  Write-Host "origin remote OK: $originUrl"
}

# Przełącz/utwórz gałąź
$curBranch = (git rev-parse --abbrev-ref HEAD).Trim()
Write-Host "Current branch: $curBranch"
if ($curBranch -ne $Branch) {
  # Czy gałąź istnieje lokalnie?
  $existsLocal = git branch --list $Branch
  if ([string]::IsNullOrWhiteSpace($existsLocal)) {
    Write-Host "Creating local branch: $Branch"
    git checkout -b $Branch
  } else {
    Write-Host "Switching to branch: $Branch"
    git checkout $Branch
  }
}

# Katalog backupu
$ts = Get-Date -Format "yyyyMMdd_HHmmss"
$backupDir = Join-Path $RepoPath ".backups\$ts"
New-Item -ItemType Directory -Force -Path $backupDir | Out-Null

# Lista plików do backupu
$files = @(
  "Kincony_VPC_M0701S.ino",
  "inverter_master_append_multi_autodetect_v21a1711_Version4.ino",
  "VPC_Modbus.cpp",
  "VPC_Modbus.h",
  "backup_precompile.ps1",
  "commit_and_push_after_test.ps1"
)

foreach($f in $files){
  if(Test-Path $f){
    Copy-Item $f -Destination $backupDir -Force
    Write-Host "Backed up: $f"
  } else {
    Write-Warning "File not found, skipping: $f"
  }
}

# Snapshot stanu repo
git status > (Join-Path $backupDir "git_status.txt")
git log -n 5 --oneline > (Join-Path $backupDir "git_log.txt")
git rev-parse HEAD > (Join-Path $backupDir "HEAD.txt")

Write-Host "Backup done: $backupDir"