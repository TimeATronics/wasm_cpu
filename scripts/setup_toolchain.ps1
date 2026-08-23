<#
.SYNOPSIS
    Install the open-source FPGA toolchain for Tang Nano 9K (Gowin GW1NR-9)
    on Windows using oss-cad-suite (pre-built binaries).

.DESCRIPTION
    Installs:
      - oss-cad-suite: Yosys, nextpnr-gowin/himbaechel, gowin_pack, etc.
      - Python packages: apycula, symbiyosys
      - openFPGALoader: FPGA programming tool
    Optionally: Icarus Verilog (iverilog) for simulation

    Run this script from PowerShell as Administrator.
#>

$ErrorActionPreference = "Continue"
$ProgressPreference = "SilentlyContinue"

# ── Configuration ────────────────────────────────────────────
$OSS_CAD_VERSION = "2025-02-14"   # Latest stable release
$OSS_CAD_URL     = "https://github.com/YosysHQ/oss-cad-suite-build/releases/download/$OSS_CAD_VERSION/oss-cad-suite-windows-x64-$OSS_CAD_VERSION.exe"
$OPENFPGALOADER_URL = "https://github.com/trabucayre/openFPGALoader/releases/download/v0.12.0/openFPGALoader-win-x64-v0.12.0.zip"

$EXTRACT_DIR = Join-Path $env:USERPROFILE "oss-cad-suite"
$OPENFPGALOADER_DIR = Join-Path $env:USERPROFILE "openFPGALoader"

Write-Host "╔══════════════════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "║  Tang Nano 9K FPGA Toolchain Installer          ║" -ForegroundColor Cyan
Write-Host "║  WASM-S32 / Lisp Stack Machine                  ║" -ForegroundColor Cyan
Write-Host "╚══════════════════════════════════════════════════╝" -ForegroundColor Cyan
Write-Host ""

# ── 1. Check prerequisites ──────────────────────────────────

Write-Host "`n[1/6] Checking prerequisites..." -ForegroundColor Yellow
$prereqsOk = $true

# Python
try {
    $pyVer = python --version 2>&1
    Write-Host "  ✓ Python: $pyVer" -ForegroundColor Green
} catch {
    Write-Host "  ✗ Python not found. Install Python 3.9+ from https://python.org" -ForegroundColor Red
    $prereqsOk = $false
}

# Git
try {
    $gitVer = git --version 2>&1
    Write-Host "  ✓ Git: $gitVer" -ForegroundColor Green
} catch {
    Write-Host "  ✗ Git not found. Install Git from https://git-scm.com" -ForegroundColor Red
    $prereqsOk = $false
}

# GCC (MinGW / MSYS2 / WSL)
try {
    $gccVer = gcc --version 2>&1 | Select-Object -First 1
    Write-Host "  ✓ GCC: $gccVer" -ForegroundColor Green
} catch {
    Write-Host "  ⚠ GCC not found on PATH. sim_s32.c won't compile. Install MSYS2 or WSL."
    Write-Host "    Download: https://github.com/msys2/msys2-installer/releases"
}

# Node.js (for assembler.js)
try {
    $nodeVer = node --version 2>&1
    Write-Host "  ✓ Node.js: $nodeVer" -ForegroundColor Green
} catch {
    Write-Host "  ⚠ Node.js not found. Needed for assembler.js. Install from https://nodejs.org"
}

if (-not $prereqsOk) {
    Write-Host "`n❌ Fix prerequisites above and re-run this script." -ForegroundColor Red
    exit 1
}

# ── 2. Clean up any old installations ────────────────────────

Write-Host "`n[2/6] Cleaning old installations..." -ForegroundColor Yellow

if (Test-Path "$EXTRACT_DIR") {
    Write-Host "  Removing old oss-cad-suite at $EXTRACT_DIR..."
    Remove-Item -Recurse -Force "$EXTRACT_DIR" -ErrorAction SilentlyContinue
}

# ── 3. Download and install oss-cad-suite ────────────────────

Write-Host "`n[3/6] Installing oss-cad-suite (Yosys + nextpnr + gowin_pack)..." -ForegroundColor Yellow

$tempExe = Join-Path $env:TEMP "oss-cad-suite-installer.exe"

try {
    Write-Host "  Downloading from $OSS_CAD_URL ..."
    $wc = New-Object System.Net.WebClient
    $wc.DownloadFile($OSS_CAD_URL, $tempExe)
    Write-Host "  Download complete." -ForegroundColor Green
} catch {
    Write-Host "  Download failed: $_" -ForegroundColor Red
    Write-Host "  Trying alternative: please manually download from:"
    Write-Host "    https://github.com/YosysHQ/oss-cad-suite-build/releases"
    Write-Host "  Choose: oss-cad-suite-windows-x64-$OSS_CAD_VERSION.exe"
    Write-Host "  Run it and set install path to: $EXTRACT_DIR"
    Write-Host "  Then re-run this script."
    exit 1
}

# oss-cad-suite is a self-extracting 7z archive. Rename and extract.
Write-Host "  Extracting to $EXTRACT_DIR ..."
New-Item -ItemType Directory -Force -Path "$EXTRACT_DIR" | Out-Null

# The .exe is actually a 7z SFX. Use 7z if available, else run it.
try {
    $has7z = Get-Command "7z" -ErrorAction SilentlyContinue
    if ($has7z) {
        & 7z x "$tempExe" -o"$EXTRACT_DIR" -y | Out-Null
    } else {
        # Just run the self-extractor
        Start-Process -Wait -FilePath "$tempExe" -ArgumentList "/S /D=$EXTRACT_DIR"
    }
    Write-Host "  Extraction complete." -ForegroundColor Green
} catch {
    Write-Host "  Extraction failed: $_" -ForegroundColor Red
    Write-Host "  Try manually: run the downloaded .exe and set path to $EXTRACT_DIR"
    exit 1
}

# Clean up installer
Remove-Item "$tempExe" -ErrorAction SilentlyContinue

# ── 4. Add to PATH ──────────────────────────────────────────

Write-Host "`n[4/6] Adding tools to system PATH..." -ForegroundColor Yellow

$ossBinPath = Join-Path $EXTRACT_DIR "bin"
$currentPath = [Environment]::GetEnvironmentVariable("Path", "User")

if ($currentPath -notlike "*$ossBinPath*") {
    $newPath = "$ossBinPath;$currentPath"
    [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
    Write-Host "  ✓ Added $ossBinPath to PATH" -ForegroundColor Green
} else {
    Write-Host "  ✓ Already in PATH" -ForegroundColor Green
}

# Also add to current session
$env:Path = "$ossBinPath;$env:Path"

# ── 5. Install Python packages ───────────────────────────────

Write-Host "`n[5/6] Installing Python packages..." -ForegroundColor Yellow

$packages = @(
    @{name="apycula"; desc="Gowin BBA support for nextpnr-himbaechel"},
    @{name="symbiyosys"; desc="Formal verification framework"},
    @{name="pyserial"; desc="Serial port communication"},
    @{name="pyelftools"; desc="ELF binary parsing"}
)

foreach ($pkg in $packages) {
    Write-Host "  Installing $($pkg.name)... ($($pkg.desc))"
    $result = pip install $pkg.name 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Host "    ✓ $($pkg.name) installed" -ForegroundColor Green
    } else {
        Write-Host "    ⚠ $($pkg.name) install had issues. Output:" -ForegroundColor Yellow
        Write-Host "    $result"
    }
}

# ── 6. Install openFPGALoader ──────────────────────────────

Write-Host "`n[6/6] Installing openFPGALoader (FPGA programmer)..." -ForegroundColor Yellow

try {
    if (-not (Test-Path "$OPENFPGALOADER_DIR")) {
        New-Item -ItemType Directory -Force -Path "$OPENFPGALOADER_DIR" | Out-Null
    }

    $tempZip = Join-Path $env:TEMP "openfpgaloader.zip"
    Write-Host "  Downloading from $OPENFPGALOADER_URL ..."
    $wc = New-Object System.Net.WebClient
    $wc.DownloadFile($OPENFPGALOADER_URL, $tempZip)

    # Extract
    Expand-Archive -Path "$tempZip" -DestinationPath "$OPENFPGALOADER_DIR" -Force
    Remove-Item "$tempZip" -ErrorAction SilentlyContinue

    # Find openFPGALoader.exe
    $ofpExe = Get-ChildItem -Path "$OPENFPGALOADER_DIR" -Recurse -Filter "openFPGALoader.exe" | Select-Object -First 1
    if ($ofpExe) {
        $ofpDir = $ofpExe.Directory.FullName
        $currentPath = [Environment]::GetEnvironmentVariable("Path", "User")
        if ($currentPath -notlike "*$ofpDir*") {
            $newPath = "$ofpDir;$currentPath"
            [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
            $env:Path = "$ofpDir;$env:Path"
        }
        Write-Host "  ✓ openFPGALoader installed at $ofpDir" -ForegroundColor Green
    }
} catch {
    Write-Host "  ⚠ openFPGALoader download/install failed: $_" -ForegroundColor Yellow
    Write-Host "  You can manually download from:"
    Write-Host "    https://github.com/trabucayre/openFPGALoader/releases"
    Write-Host "  Extract and add the folder with openFPGALoader.exe to PATH."
}

# ── Verification ──────────────────────────────────────────────

Write-Host ""
Write-Host "╔══════════════════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "║  Verifying Installation                          ║" -ForegroundColor Cyan
Write-Host "╚══════════════════════════════════════════════════╝" -ForegroundColor Cyan
Write-Host ""

$tools = @("yosys", "nextpnr-himbaechel", "gowin_pack", "openFPGALoader")
$allOk = $true

foreach ($tool in $tools) {
    try {
        $ver = & $tool --version 2>&1 | Select-Object -First 1
        if (-not $ver) { $ver = & $tool -V 2>&1 | Select-Object -First 1 }
        if (-not $ver) { $ver = & $tool version 2>&1 | Select-Object -First 1 }
        Write-Host "  ✓ $tool : $ver" -ForegroundColor Green
    } catch {
        Write-Host "  ✗ $tool : NOT FOUND" -ForegroundColor Red
        $allOk = $false
    }
}

# Check Python packages
$pyPkgs = @("apycula", "symbiyosys")
foreach ($pkg in $pyPkgs) {
    $result = python -c "import $pkg; print('$pkg version:', $pkg.__version__)" 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  ✓ $(python -c "import $pkg; print($pkg.__version__)")" -ForegroundColor Green
    } else {
        Write-Host "  ✗ $pkg : NOT FOUND (pip install $pkg)" -ForegroundColor Yellow
    }
}

Write-Host ""
if ($allOk) {
    Write-Host "✅ All core tools installed successfully!" -ForegroundColor Green
    Write-Host ""
    Write-Host "Next steps:"
    Write-Host "  1. Close and reopen PowerShell/Terminal for PATH changes to take effect"
    Write-Host "  2. Test with:  cd $PWD && .\build.bat synth"
    Write-Host "  3. For the Lisp compiler:  pip install pytest   (for running tests)"
    Write-Host "  4. Connect Tang Nano 9K via USB and run:  .\build.bat program"
} else {
    Write-Host "⚠ Some tools had issues. See individual messages above." -ForegroundColor Yellow
}