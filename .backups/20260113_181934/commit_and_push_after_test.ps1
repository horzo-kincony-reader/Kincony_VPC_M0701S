param(
  [string]$RepoPath    = "D:\KINCONY\ARDIUNO\GithubCopilot\REPOSITORY\Kincony_VPC_M0701S",
  [string]$Branch      = "feature/vpc-m0701s",
  [string]$RemoteOwner = "horzo-kincony-reader",
  [string]$RepoName    = "Kincony_VPC_M0701S",
  [string]$Message     = "VPC M0701S: update after local tests",
  [switch]$FixRemote   # Użyj, aby automatycznie ustawić remote origin na prawidłowy URL, jeśli mismatch
)

# Rygorystyczne ustawienia PowerShell
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Write-Host "== Commit & Push after test =="

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

# Upewnij się, że gałąź istnieje i jest aktywna
# Sprawdź czy gałąź istnieje zdalnie
$remoteExists = git rev-parse --verify "origin/$Branch" 2>&1
$remoteBranchExists = ($LASTEXITCODE -eq 0)

# Sprawdź czy gałąź istnieje lokalnie
$localExists = git branch --list $Branch 2>&1
$localBranchExists = -not [string]::IsNullOrWhiteSpace($localExists)

if ($remoteBranchExists) {
  Write-Host "Remote branch 'origin/$Branch' exists. Creating/updating local branch to track it..."
  git checkout -B $Branch "origin/$Branch"
  if ($LASTEXITCODE -ne 0) {
    throw "Failed to checkout branch $Branch from origin/$Branch"
  }
  Write-Host "Switched to branch '$Branch' (tracking origin/$Branch)"
} elseif ($localBranchExists) {
  $curBranch = git rev-parse --abbrev-ref HEAD 2>&1
  if ($LASTEXITCODE -ne 0) {
    throw "Failed to get current branch"
  }
  $curBranch = $curBranch.Trim()
  if ($curBranch -ne $Branch) {
    Write-Host "Switching to existing local branch: $Branch"
    git checkout $Branch
    if ($LASTEXITCODE -ne 0) {
      throw "Failed to checkout local branch $Branch"
    }
  } else {
    Write-Host "Already on branch: $Branch"
  }
} else {
  Write-Host "Branch '$Branch' does not exist locally or remotely. Creating new local branch from current HEAD..."
  git checkout -b $Branch
  if ($LASTEXITCODE -ne 0) {
    throw "Failed to create new branch $Branch"
  }
  Write-Warning "Created new local branch '$Branch' from HEAD. This branch is not tracking any remote branch yet."
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

$stagedCount = 0
foreach($f in $files){
  if(Test-Path $f){
    git add $f
    if ($LASTEXITCODE -eq 0) {
      Write-Host "Staged: $f"
      $stagedCount++
    } else {
      Write-Warning "Failed to stage: $f"
    }
  } else {
    Write-Warning "File not found, skipping: $f"
  }
}

Write-Host "Total files staged: $stagedCount"

# Commit (obsługa braku zmian)
$pending = git diff --cached --name-only 2>&1
if ($LASTEXITCODE -ne 0) {
  Write-Warning "Failed to check staged changes. Attempting commit anyway..."
}

if ([string]::IsNullOrWhiteSpace($pending)) {
  Write-Warning "No staged changes to commit."
} else {
  Write-Host "Committing changes with message: '$Message'"
  git commit -m $Message
  if ($LASTEXITCODE -ne 0) {
    throw "Commit failed"
  }
  Write-Host "Commit successful."
}

# Ustaw upstream i push
Write-Host "Pushing to origin/$Branch ..."
$pushOutput = git push -u origin $Branch 2>&1
$pushExitCode = $LASTEXITCODE

if ($pushExitCode -eq 0) {
  Write-Host "Push successful."
} else {
  Write-Error "Push failed with exit code: $pushExitCode"
  Write-Host ""
  Write-Host "Push output:"
  Write-Host $pushOutput
  Write-Host ""
  Write-Host "Possible reasons and solutions:"
  Write-Host "  - Remote branch has changes not present locally"
  Write-Host "    Solution: Run 'git pull --rebase origin $Branch' then push again"
  Write-Host "  - Authentication failed"
  Write-Host "    Solution: Check your credentials and network connection"
  Write-Host "  - Remote rejected the push"
  Write-Host "    Solution: Check repository permissions and branch protection rules"
  throw "Push operation failed"
}