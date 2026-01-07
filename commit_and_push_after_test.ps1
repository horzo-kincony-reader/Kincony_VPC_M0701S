<#
commit_and_push_after_test.ps1

Opis:
  Po poprawnej kompilacji/testach skrypt:
  - tworzy nową gałąź tested/master-<TS> (bazując na main lub aktualnej gałęzi)
  - dodaje i commit'uje zmiany tylko z katalogu TargetDir
  - wypycha gałąź na zdalny origin
  - tworzy/aktualizuje plik .last_tested.json w repo z metadanymi (timestamp, branch, commit SHA, targetDir, file)
  - commituje i wypycha również .last_tested.json na tę samą gałąź
  - NIE scala automatycznie do main (bezpieczne)
Użycie:
  powershell -ExecutionPolicy Bypass -File .\commit_and_push_after_test.ps1
  .\commit_and_push_after_test.ps1 -RepoRoot "D:\...\Kincony_Delta_me300" -Message "Tested on device, works"
Parametry:
  -RepoRoot : katalog repo (domyślnie bieżący)
  -TargetDir: katalog do commitowania (domyślnie master_kc868...)
  -Remote   : remote (domyślnie origin)
  -Message  : (opcjonalne) commit message, jeśli brak - generowany automatycznie
#>

param(
  [string]$RepoRoot = ".",
  [string]$TargetDir = "master_kc868_a16_multi_sid_v21a_fixed_Version2_Version22",
  [string]$Remote = "origin",
  [string]$Message = ""
)

Set-StrictMode -Version Latest

# Resolve and check repo path
try {
  $repoPathInfo = Resolve-Path -Path $RepoRoot -ErrorAction Stop
  $repoPath = $repoPathInfo.Path
} catch {
  Write-Error "Repo root nie istnieje lub nie można go zlokalizować: $RepoRoot"
  exit 1
}

Push-Location $repoPath

# Verify git repo
git rev-parse --git-dir > $null 2>&1
if ($LASTEXITCODE -ne 0) {
  Write-Error "To nie jest repozytorium git: $repoPath"
  Pop-Location
  exit 1
}

# Timestamp and branch name
$ts = Get-Date -Format "yyyyMMdd-HHmmss"
$branchName = "tested/master-$ts"

# Fetch remote (do not fail the script if fetch has issues)
git fetch $Remote > $null 2>&1

# Determine base branch: prefer local 'main' if present, otherwise use current branch
$baseBranch = "main"
git show-ref --verify --quiet "refs/heads/$baseBranch" > $null 2>&1
$hasMain = ($LASTEXITCODE -eq 0)

if ($hasMain) {
  git checkout $baseBranch 2>$null
  if ($LASTEXITCODE -eq 0) {
    git pull $Remote $baseBranch --ff-only 2>$null
  } else {
    Write-Warning "Nie udało się checkoutować $baseBranch, kontynuuję na aktualnej gałęzi."
  }
} else {
  $baseBranch = git rev-parse --abbrev-ref HEAD 2>$null
  Write-Host "Gałąź 'main' nie istnieje. Używam aktualnej gałęzi jako bazy: $baseBranch"
}

# Create new feature branch from base
git checkout -b $branchName
if ($LASTEXITCODE -ne 0) {
  Write-Error "Nie udało się utworzyć gałęzi $branchName"
  Pop-Location
  exit 1
}

# Add and commit only TargetDir (if exists)
$fullTargetPath = Join-Path $repoPath $TargetDir
if (Test-Path $fullTargetPath) {
  git add --all -- "$TargetDir"
} else {
  Write-Warning "Katalog docelowy nie istnieje lokalnie: $TargetDir - nic do dodania."
}

# Default commit message if not provided
if ([string]::IsNullOrWhiteSpace($Message)) {
  $Message = "Tested: update $TargetDir - verified locally $ts"
}

# Check staged changes
$staged = git diff --cached --name-only
# Zawsze zainicjalizuj jako pust� tablic�
$stagedList = @()
if ($staged) {
  $stagedList = ($staged -split "`n") | Where-Object { $_ -ne "" }
}

if ($stagedList.Count -gt 0) {
  git commit -m $Message
  if ($LASTEXITCODE -ne 0) {
    Write-Error "Commit nie powi�d� si�."
    git checkout -
    Pop-Location
    exit 1
  } else {
    Write-Host "Zatwierdzono zmiany w $TargetDir na branchu $branchName"
  }
} else {
  Write-Host "Brak zmian w $TargetDir do zatwierdzenia. Nie tworz� commita."
  git checkout -
  Pop-Location
  exit 0
}

# Push branch to remote
git push -u $Remote $branchName
if ($LASTEXITCODE -ne 0) {
  Write-Error "Push branch $branchName nie powiódł się."
  Pop-Location
  exit 1
}

# Get commit SHA of last commit
$commitSha = git rev-parse HEAD 2>$null
if ($LASTEXITCODE -ne 0) {
  Write-Warning "Nie udało się pobrać commit SHA"
  $commitSha = ""
}

# Create/update metadata file .last_tested.json in repo root
$meta = @{
  timestamp = $ts
  branch = $branchName
  targetDir = $TargetDir
  commit = $commitSha
  file = Join-Path $TargetDir "master_kc868_a16_multi_sid_v21a_fixed_Version2_Version22.ino"
}
$metaJson = $meta | ConvertTo-Json -Depth 4

$metaPath = Join-Path $repoPath ".last_tested.json"
# Write metadata file (UTF8)
Set-Content -Path $metaPath -Value $metaJson -Encoding UTF8

# Add and commit metadata (on the same branch)
git add .last_tested.json
git commit -m "Update last tested metadata $ts" 2>$null
if ($LASTEXITCODE -ne 0) {
  Write-Warning "Commit metadanych nie powiódł się lub brak zmian (może plik nie zmienił się). Kontynuuję..."
} else {
  Write-Host "Zatwierdzono .last_tested.json"
}

# Push metadata commit (may be same branch)
git push $Remote $branchName
if ($LASTEXITCODE -ne 0) {
  Write-Warning "Push metadanych nie powiódł się. Sprawdź połączenie z remote."
} else {
  Write-Host "Metadane zostały wypchnięte na $Remote/$branchName"
}

Write-Host ""
Write-Host "Zaktualizowana wersja wysłana jako gałąź: $Remote/$branchName"
Write-Host "Metadata file: .last_tested.json (commit: $commitSha)"
Write-Host "Od teraz będę analizować tę wersję (najnowszą przetestowaną) po otrzymaniu Twojego potwierdzenia."

Pop-Location