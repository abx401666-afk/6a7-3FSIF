$ErrorActionPreference = 'Stop'

# Run this script from the extracted project folder. It initializes Git if needed,
# then adds the third-party dependencies as real Git submodules.
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ProjectRoot

if (-not (Test-Path 'BodycamMod.sln')) {
    throw 'Run this script from the BodycamMod project folder (BodycamMod.sln must be present).'
}

# Ensure Git is available.
$git = Get-Command git -ErrorAction SilentlyContinue
if (-not $git) {
    throw 'Git for Windows is required. Install Git, reopen PowerShell, and run this script again.'
}

# A Git repository is needed before Git can create submodules.
& git rev-parse --show-toplevel *> $null
if ($LASTEXITCODE -ne 0) {
    & git init
    if ($LASTEXITCODE -ne 0) { throw 'git init failed.' }
}

function Test-SubmodulePath([string]$Path) {
    if (-not (Test-Path '.gitmodules')) { return $false }
    $content = Get-Content '.gitmodules' -Raw
    $escaped = [regex]::Escape($Path)
    return ($content -match "(?m)^\s*path\s*=\s*$escaped\s*$")
}

New-Item -ItemType Directory -Force -Path 'vendor' | Out-Null

if (-not (Test-SubmodulePath 'vendor/imgui')) {
    if (Test-Path 'vendor/imgui') {
        throw "'vendor/imgui' exists but is not registered as a submodule. Move or remove it first."
    }
    Write-Host 'Adding Dear ImGui as a submodule...'
    & git submodule add --branch master https://github.com/ocornut/imgui.git vendor/imgui
    if ($LASTEXITCODE -ne 0) { throw 'Could not add the Dear ImGui submodule.' }
}
& git -C vendor/imgui checkout --detach v1.93.0
if ($LASTEXITCODE -ne 0) { throw 'Could not check out Dear ImGui v1.93.0.' }

if (-not (Test-SubmodulePath 'vendor/minhook')) {
    if (Test-Path 'vendor/minhook') {
        throw "'vendor/minhook' exists but is not registered as a submodule. Move or remove it first."
    }
    Write-Host 'Adding MinHook as a submodule...'
    & git submodule add --branch master https://github.com/TsudaKageyu/minhook.git vendor/minhook
    if ($LASTEXITCODE -ne 0) { throw 'Could not add the MinHook submodule.' }
}
& git -C vendor/minhook checkout --detach 8af6b4a
if ($LASTEXITCODE -ne 0) { throw 'Could not check out the MinHook revision matching the vendored HDE sources.' }

& git add .gitmodules vendor/imgui vendor/minhook
if ($LASTEXITCODE -ne 0) { throw 'Could not stage the submodule entries.' }

Write-Host ''
Write-Host 'Submodules are set up and staged.'
Write-Host 'Next run: git add .'
Write-Host 'Then review: git status'
Write-Host 'Then commit: git commit -m "Prepare /var source with Git submodules"'
