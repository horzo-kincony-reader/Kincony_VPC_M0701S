<#
backup_precompile.ps1
Opis:
  Tworzy zdalny "snapshot" katalogu (commit na nowym branchu backup/pre-compile-<TS>)
  - pracuje z katalogu repo (domyślnie bieżący katalog)
  - dotyczy wyłącznie wskazanego katalogu (nie dotyka innych plików)
  - jeżeli są niezatwierdzone zmiany w tym katalogu, zostaną dodane do commita
  - jeśli nie ma żadnych zmian, zostanie utworzony pusty commit (domyślnie)
Użycie:
  powershell -ExecutionPolicy Bypass -File .\backup_precompile.ps1
  .\backup_precompile.ps1 -RepoRoot "D:\... \Kincony_Delta_me300"
Parametry:
  -RepoRoot  : (opcjonalne) ścieżka do katalogu repo (domyślnie bieżący katalog).
  -TargetDir : (opcjonalne) względna ścieżka katalogu do backupu (domyślnie katalog master_kc868...).
  -Remote    : (opcjonalne) nazwa remote (domyślnie 'origin').
  -AllowEmptyCommit : Switch - tworzy pusty commit jeśli brak zmian (domyślnie $true).
#>

param(
  [string]$RepoRoot = ".",
  [string]$TargetDir = "master_kc868_a16_multi_sid_v21a_fixed_Version2_Version22",
  [string]$Remote = "origin",
  [switch]$AllowEmptyCommit = $true
)

Set-StrictMode -Version Latest

try {
  $repoPathInfo = Resolve-Path -Path $RepoRoot -ErrorAction Stop
  $repoPath = $repoPathInfo.Path
} catch {
  Write-Error "Repo root nie istnieje lub nie można go zlokalizować: $RepoRoot"
  exit 1
}

Push-Location $repoPath

# Sprawdź czy to repo git
git rev-parse --git-dir > $null 2>&1
if ($LASTEXITCODE -ne 0) {
  Write-Error "To nie jest repozytorium git: $repoPath"
  Pop-Location
  exit 1
}

# Normalize target dir and check existence (relative to repo root)
$targetPath = Join-Path $repoPath $TargetDir
if (-not (Test-Path $targetPath)) {
  Write-Warning "Uwaga: katalog docelowy nie istnieje lokalnie: $TargetDir"
  # dalej wykonamy pusty commit lub branch mimo wszystko (zgodnie z AllowEmptyCommit)
}

# Timestamp
$ts = Get-Date -Format "yyyyMMdd-HHmmss"
$branchName = "backup/pre-compile-$ts"

# Fetch remote (nie przerywamy przy błędzie fetch)
git fetch $Remote > $null 2>&1

# Sprawdź, czy istnieje lokalna gałąź main
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
  $current = git rev-parse --abbrev-ref HEAD 2>$null
  $baseBranch = $current
  Write-Host "Gałąź 'main' nie znaleziona. Używam gałęzi bazowej: $baseBranch"
}

# Utwórz branch backupowy
git checkout -b $branchName
if ($LASTEXITCODE -ne 0) {
  Write-Error "Nie udało się utworzyć gałęzi $branchName"
  Pop-Location
  exit 1
}

# Dodaj tylko katalog docelowy (jeśli istnieje)
if (Test-Path $targetPath) {
  git add --all -- "$TargetDir"
} else {
  Write-Host "Katalog docelowy nie istnieje lokalnie - nie dodano plików (możliwe utworzenie pustego commita)."
}

# Sprawdź czy są staged changes
$staged = git diff --cached --name-only
$stagedList = @()
if ($staged) { $stagedList = $staged -split "`n" | Where-Object { $_ -ne "" } }

if ($stagedList.Count -gt 0) {
  $msg = "Backup ($TargetDir) pre-compile $ts"
  git commit -m $msg
  if ($LASTEXITCODE -ne 0) {
    Write-Error "Commit nie powiódł się."
    git checkout -
    Pop-Location
    exit 1
  } else {
    Write-Host "Committed staged changes for $TargetDir on branch $branchName"
  }
} else {
  if ($AllowEmptyCommit.IsPresent) {
    $msg = "Backup (empty) ($TargetDir) pre-compile $ts"
    git commit --allow-empty -m $msg > $null 2>&1
    if ($LASTEXITCODE -ne 0) {
      Write-Error "Utworzenie pustego commita nie powiodło się."
      git checkout -
      Pop-Location
      exit 1
    } else {
      Write-Host "Utworzono pusty commit jako punkt backupu na branchu $branchName"
    }
  } else {
    Write-Host "Brak zmian do zatwierdzenia i AllowEmptyCommit = false. Tworzę branch bez commita."
  }
}

# Push branch
git push -u $Remote $branchName
if ($LASTEXITCODE -ne 0) {
  Write-Error "Push branch $branchName nie powiódł się."
  Pop-Location
  exit 1
}

Write-Host "Backup zapisany na zdalnym repo jako gałąź: $Remote/$branchName"

Pop-Location