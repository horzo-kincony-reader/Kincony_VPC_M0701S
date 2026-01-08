# Backup przed kompilacją – uwzględnia gałąź feature/vpc-m0701s
param(
  [string]$RepoPath = "D:\KINCONY\ARDIUNO\GithubCopilot\REPOSITORY\Kincony_VPC_M0701S",
  [string]$Branch = "feature/vpc-m0701s"
)

Write-Host "== Backup precompile =="

# Przejście do katalogu repozytorium
Set-Location -Path $RepoPath

# Sprawdź bieżącą gałąź
$curBranch = git rev-parse --abbrev-ref HEAD
Write-Host "Current branch: $curBranch"
if ($curBranch -ne $Branch) {
  Write-Host "Switching to $Branch..."
  git checkout $Branch 2>$null
}

# Katalog backupów
$ts = Get-Date -Format "yyyyMMdd_HHmmss"
$backupDir = Join-Path $RepoPath ".backups\$ts"
New-Item -ItemType Directory -Force -Path $backupDir | Out-Null

# Zestaw plików do backupu
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
  }
}

# Snapshot stanu repo (opcjonalnie)
git status > (Join-Path $backupDir "git_status.txt")
git log -n 5 --oneline > (Join-Path $backupDir "git_log.txt")

Write-Host "Backup done: $backupDir"