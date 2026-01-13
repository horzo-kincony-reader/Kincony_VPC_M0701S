# Commit + push po testach – ustawia upstream dla feature/vpc-m0701s
param(
  [string]$RepoPath = "D:\KINCONY\ARDIUNO\GithubCopilot\REPOSITORY\Kincony_VPC_M0701S",
  [string]$Branch = "feature/vpc-m0701s",
  [string]$Message = "VPC M0701S: update after local tests"
)

Write-Host "== Commit & Push after test =="

Set-Location -Path $RepoPath

# Upewnij się że gałąź istnieje i jest aktywna
$branches = git branch --list $Branch
if (-not $branches) {
  Write-Host "Creating branch $Branch..."
  git checkout -b $Branch
} else {
  $curBranch = git rev-parse --abbrev-ref HEAD
  if ($curBranch -ne $Branch) {
    Write-Host "Switching to $Branch..."
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
  }
}

# Commit
git commit -m $Message

# Ustaw upstream i push
Write-Host "Pushing to origin/$Branch ..."
git push -u origin $Branch

Write-Host "Done."