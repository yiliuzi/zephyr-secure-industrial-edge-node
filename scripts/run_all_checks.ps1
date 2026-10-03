$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$python = "D:\zephyr-workspace\.venv313\Scripts\python.exe"
$env:ZEPHYR_BASE = "D:\zephyr-workspace\zephyrproject\zephyr"
$qemu = "D:\qemu\qemu-system-arm.exe"

foreach ($path in @(
    $python,
    $qemu,
    "$env:ZEPHYR_BASE\CMakeLists.txt"
)) {
    if (-not (Test-Path $path)) {
        throw "Required path not found: $path"
    }
}

Push-Location $projectRoot
try {
    & $python .\scripts\run_regression.py --qemu $qemu --timeout 60
    if ($LASTEXITCODE -ne 0) {
        throw "Regression tests failed."
    }

    & $python .\scripts\run_runtime_smoke.py
    if ($LASTEXITCODE -ne 0) {
        throw "Runtime smoke test failed."
    }

    & $python .\scripts\run_queue_overload.py
    if ($LASTEXITCODE -ne 0) {
        throw "Queue overload test failed."
    }

    Write-Host "`nALL CHECKS PASSED" -ForegroundColor Green
}
finally {
    Pop-Location
}
