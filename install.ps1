# Nova Programming Language — Install Script (Windows PowerShell)
# Usage: irm <raw-url>/install.ps1 | iex
$ErrorActionPreference = "Stop"

$RepoUrl = "https://github.com/Cosmic4796/Nova.git"
$InstallDir = "$env:ProgramFiles\Nova"
$TmpDir = Join-Path $env:TEMP "nova-install-$(Get-Random)"

Write-Host "==> Installing Nova Programming Language..." -ForegroundColor Cyan

# Check dependencies
foreach ($cmd in @("git", "cmake")) {
    if (-not (Get-Command $cmd -ErrorAction SilentlyContinue)) {
        Write-Host "Error: '$cmd' is required but not found." -ForegroundColor Red
        Write-Host "Please install it and try again."
        exit 1
    }
}

# Clone repository
Write-Host "==> Cloning repository..."
git clone --quiet --depth 1 $RepoUrl "$TmpDir\nova" 2>$null
if (-not $?) {
    Write-Host "Error: Failed to clone repository." -ForegroundColor Red
    Remove-Item -Recurse -Force $TmpDir -ErrorAction SilentlyContinue
    exit 1
}

# Build
Write-Host "==> Building Nova..."
Push-Location "$TmpDir\nova"
cmake -B build -DCMAKE_BUILD_TYPE=Release 2>$null | Out-Null
cmake --build build --config Release 2>$null | Out-Null
Pop-Location

# Install
Write-Host "==> Installing to $InstallDir..."
New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
New-Item -ItemType Directory -Path "$InstallDir\stdlib" -Force | Out-Null

Copy-Item "$TmpDir\nova\build\Release\nova.exe" "$InstallDir\nova.exe" -Force -ErrorAction SilentlyContinue
if (-not (Test-Path "$InstallDir\nova.exe")) {
    Copy-Item "$TmpDir\nova\build\nova.exe" "$InstallDir\nova.exe" -Force
}
Copy-Item "$TmpDir\nova\build\stdlib\*" "$InstallDir\stdlib\" -Force

# Add to PATH
$currentPath = [Environment]::GetEnvironmentVariable("Path", "Machine")
if ($currentPath -notlike "*$InstallDir*") {
    Write-Host "==> Adding Nova to system PATH..."
    [Environment]::SetEnvironmentVariable("Path", "$currentPath;$InstallDir", "Machine")
    $env:Path = "$env:Path;$InstallDir"
}

# Cleanup
Remove-Item -Recurse -Force $TmpDir -ErrorAction SilentlyContinue

# Verify
Write-Host ""
Write-Host "==> Nova installed successfully!" -ForegroundColor Green
& "$InstallDir\nova.exe" version
Write-Host ""
Write-Host "Get started:"
Write-Host "  nova              # Start the REPL"
Write-Host "  nova init myapp   # Create a new project"
Write-Host "  nova help         # Show all commands"
Write-Host ""
Write-Host "Note: You may need to restart your terminal for PATH changes." -ForegroundColor Yellow
