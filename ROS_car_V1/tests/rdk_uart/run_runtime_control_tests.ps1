param([ValidateSet('Debug','Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'native_environment.ps1')
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$outputDirectory = Join-Path $projectRoot "build/rdk_uart_tests/$Configuration"
$optimization = if ($Configuration -eq 'Release') { '-Os' } else { '-O0' }
$includeDirectory = Join-Path $projectRoot 'Core/Inc'
$mockHeader = Join-Path $PSScriptRoot 'mock_hal.h'
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$objects = @()
foreach ($module in @('rdk_uart','uart_dma_rx','imu_attitude','ELRS','drive_control','Mecanum')) {
    $source = Join-Path $projectRoot "Core/Src/$module.c"
    $object = Join-Path $outputDirectory "$module.o"
    & gcc $optimization -std=c11 -Wall -Wextra -Werror -include $mockHeader -I $includeDirectory -c $source -o $object
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $module" }
    $objects += $object
}
$executable = Join-Path $outputDirectory 'test_runtime_control.exe'
& g++ $optimization -std=c++17 -Wall -Wextra -Werror -include $mockHeader -I $includeDirectory (Join-Path $PSScriptRoot 'test_runtime_control.cpp') @objects -o $executable
if ($LASTEXITCODE -ne 0) { throw 'Runtime control integration compilation failed' }
& $executable
if ($LASTEXITCODE -ne 0) { throw 'Runtime control integration tests failed' }
foreach ($scenario in @('boot-radio', 'boot-radio-low', 'boot-remote')) {
    & $executable $scenario
    if ($LASTEXITCODE -ne 0) { throw "Runtime boot scenario failed: $scenario" }
}
