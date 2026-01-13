param(
  [string]$RepoPath    = "D:\KINCONY\ARDIUNO\GithubCopilot\REPOSITORY\Kincony_VPC_M0701S",
  [string]$Branch      = "feature/vpc-m0701s",
  [string]$RemoteOwner = "horzo-kincony-reader",
  [string]$RepoName    = "Kincony_VPC_M0701S",
  [switch]$FixRemote   # Użyj, aby automatycznie ustawić remote origin na prawidłowy URL, jeśli mismatch
)

# Rygorystyczne ustawienia PowerShell
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Write-Host "== Backup precompile =="

# Przejście do katalogu repozytorium
if (-not (Test-Path $RepoPath)) {
  throw "RepoPath not found: $RepoPath"
}
Set-Location -Path $RepoPath

# Weryfikacja remote origin
$expectedUrl = "https://github.com/$RemoteOwner/$RepoName.git"
$originUrl = $null
try {
  $originUrl = git remote get-url origin 2>&1
  if ($LASTEXITCODE -ne 0) { $originUrl = $null }
} catch { 
  $originUrl = $null 
}

if ([string]::IsNullOrWhiteSpace($originUrl)) {
  Write-Warning "No 'origin' remote configured. Expected: $expectedUrl"
  if ($FixRemote) {
    Write-Host "Setting origin to $expectedUrl..."
    git remote add origin $expectedUrl
    if ($LASTEXITCODE -ne 0) {
      throw "Failed to add remote origin"
    }
  } else {
    Write-Host "Tip: run with -FixRemote to add the remote automatically."
  }
} elseif ($originUrl -ne $expectedUrl) {
  Write-Warning "origin remote mismatch. Current: $originUrl; Expected: $expectedUrl"
  if ($FixRemote) {
    Write-Host "Updating origin to $expectedUrl..."
    git remote set-url origin $expectedUrl
    if ($LASTEXITCODE -ne 0) {
      throw "Failed to update remote origin"
    }
  } else {
    Write-Host "Tip: run with -FixRemote to update the remote URL automatically."
  }
} else {
  Write-Host "origin remote OK: $originUrl"
}

# Fetch z origin aby mieć aktualne informacje o zdalnych gałęziach
Write-Host "Fetching from origin to get latest branch information..."
git fetch origin 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) {
  Write-Warning "git fetch origin failed (exit code: $LASTEXITCODE). Continuing anyway..."
  # Nie przerywamy, bo może to być problem z siecią, a użytkownik może pracować offline
}

# Przełącz/utwórz gałąź
$curBranch = git rev-parse --abbrev-ref HEAD 2>&1
if ($LASTEXITCODE -ne 0) {
  throw "Failed to get current branch"
}
$curBranch = $curBranch.Trim()
Write-Host "Current branch: $curBranch"

if ($curBranch -ne $Branch) {
  # Sprawdź czy gałąź istnieje zdalnie używając ls-remote
  $remoteBranchCheck = git ls-remote --heads origin "refs/heads/$Branch" 2>&1
  $remoteBranchExists = ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($remoteBranchCheck))
  
  # Sprawdź czy gałąź istnieje lokalnie
  $localExists = git branch --list $Branch 2>&1
  $localBranchExists = -not [string]::IsNullOrWhiteSpace($localExists)
  
  if ($remoteBranchExists) {
    Write-Host "Remote branch 'origin/$Branch' exists. Fetching and creating/updating local branch to track it..."
    # Fetch the specific branch with full refspec to create remote tracking branch
    git fetch origin "+refs/heads/${Branch}:refs/remotes/origin/$Branch" 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
      Write-Warning "Failed to fetch origin/$Branch into remote tracking branch"
    }
    
    # Try to checkout the branch
    git checkout $Branch 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
      # If checkout failed, the branch doesn't exist locally, so create it
      git checkout -b $Branch "origin/$Branch" 2>&1 | Out-Null
      if ($LASTEXITCODE -ne 0) {
        throw "Failed to checkout/create branch $Branch from origin/$Branch"
      }
    }
    
    # Ensure tracking is set up (may fail in shallow clones, which is OK)
    $trackingResult = git branch -u "origin/$Branch" $Branch 2>&1
    if ($LASTEXITCODE -ne 0) {
      Write-Verbose "Note: Could not set up branch tracking (exit code: $LASTEXITCODE). This is normal in shallow clones."
    }
    
    Write-Host "Switched to branch '$Branch' (from origin/$Branch)"
  } elseif ($localBranchExists) {
    Write-Host "Switching to existing local branch: $Branch"
    git checkout $Branch
    if ($LASTEXITCODE -ne 0) {
      throw "Failed to checkout local branch $Branch"
    }
  } else {
    Write-Host "Branch '$Branch' does not exist locally or remotely. Creating new local branch from current HEAD..."
    git checkout -b $Branch
    if ($LASTEXITCODE -ne 0) {
      throw "Failed to create new branch $Branch"
    }
    Write-Warning "Created new local branch '$Branch' from HEAD. This branch is not tracking any remote branch yet."
  }
}

# Katalog backupu
$ts = Get-Date -Format "yyyyMMdd_HHmmss"
$backupDir = Join-Path $RepoPath ".backups\$ts"
try {
  New-Item -ItemType Directory -Force -Path $backupDir | Out-Null
  Write-Host "Created backup directory: $backupDir"
} catch {
  throw "Failed to create backup directory: $backupDir. Error: $_"
}

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
    try {
      Copy-Item $f -Destination $backupDir -Force
      Write-Host "Backed up: $f"
    } catch {
      Write-Warning "Failed to backup $f : $_"
    }
  } else {
    Write-Warning "File not found, skipping: $f"
  }
}

# Snapshot stanu repo
try {
  git status > (Join-Path $backupDir "git_status.txt")
  git log -n 5 --oneline > (Join-Path $backupDir "git_log.txt")
  git rev-parse HEAD > (Join-Path $backupDir "HEAD.txt")
  Write-Host "Git snapshot saved to backup directory"
} catch {
  Write-Warning "Failed to save git snapshot: $_"
}

Write-Host "Backup completed successfully: $backupDir"