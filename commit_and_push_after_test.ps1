param(
  [string]$RepoPath    = "D:\KINCONY\ARDIUNO\GithubCopilot\REPOSITORY\Kincony_VPC_M0701S",
  [string]$Branch      = "feature/vpc-m0701s",
  [string]$RemoteOwner = "horzo-kincony-reader",
  [string]$RepoName    = "Kincony_VPC_M0701S",
  [string]$Message     = "VPC M0701S: update after local tests",
  [switch]$FixRemote   # Użyj, aby automatycznie ustawić remote origin na prawidłowy URL, jeśli mismatch
)

Write-Host "== Commit & Push after test =="

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
  } else {
    Write-Host "Tip: run with -FixRemote to add the remote automatically."
  }
} elseif ($originUrl -ne $expectedUrl) {
  Write-Warning "origin remote mismatch. Current: $originUrl; Expected: $expectedUrl"
  if ($FixRemote) {
    Write-Host "Updating origin to $expectedUrl..."
    git remote set-url origin $expectedUrl
  }
} else {
  Write-Host "origin remote OK: $originUrl"
}

# Upewnij się, że gałąź istnieje i jest aktywna
$existsLocal = git branch --list $Branch
if ([string]::IsNullOrWhiteSpace($existsLocal)) {
  Write-Host "Creating local branch: $Branch"
  git checkout -b $Branch
} else {
  $curBranch = (git rev-parse --abbrev-ref HEAD).Trim()
  if ($curBranch -ne $Branch) {
    Write-Host "Switching to branch: $Branch"
    git checkout $Branch
  }
}

# Stage plików
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
    git add $f
    Write-Host "Staged: $f"
  } else {
    Write-Warning "File not found, skipping: $f"
  }
}

# Commit (obsługa braku zmian)
$pending = git diff --cached --name-only
if ([string]::IsNullOrWhiteSpace($pending)) {
  Write-Warning "No staged changes to commit."
} else {
  git commit -m $Message
}

# Ustaw upstream i push
Write-Host "Pushing to origin/$Branch ..."
try {
  git push -u origin $Branch
  Write-Host "Push OK."
} catch {
  Write-Warning "Push failed. Check credentials and remote."
}