param(
    [string]$ProtocolDirectory = (Join-Path ([Environment]::GetFolderPath('Desktop')) '.'),
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug'
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'native_environment.ps1')
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$outputDirectory = Join-Path $projectRoot "build/rdk_uart_tests/$Configuration"
$optimization = if ($Configuration -eq 'Release') { '-Os' } else { '-O0' }
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$mockHeader = Join-Path $PSScriptRoot 'mock_hal.h'
$includeDirectory = Join-Path $projectRoot 'Core/Inc'
$moduleSource = Join-Path $projectRoot 'Core/Src/rdk_uart.c'
$moduleObject = Join-Path $outputDirectory 'rdk_uart.o'
$dmaSource = Join-Path $projectRoot 'Core/Src/uart_dma_rx.c'
$dmaObject = Join-Path $outputDirectory 'uart_dma_rx.o'
$attitudeSource = Join-Path $projectRoot 'Core/Src/imu_attitude.c'
$attitudeObject = Join-Path $outputDirectory 'imu_attitude.o'
$attitudeTestSource = Join-Path $PSScriptRoot 'test_imu_attitude.cpp'
$attitudeExecutable = Join-Path $outputDirectory 'test_imu_attitude.exe'
$testSource = Join-Path $PSScriptRoot 'test_rdk_uart.cpp'
$upstreamSource = Join-Path $ProtocolDirectory 'x5car_protocol.cpp'
$executable = Join-Path $outputDirectory 'test_rdk_uart.exe'
if (-not (Test-Path -LiteralPath $upstreamSource)) {
    throw "Missing original upstream protocol: $upstreamSource"
}
& gcc $optimization -std=c11 -Wall -Wextra -Werror -include $mockHeader -I $includeDirectory -c $moduleSource -o $moduleObject
if ($LASTEXITCODE -ne 0) { throw 'Native C compilation failed' }
& gcc $optimization -std=c11 -Wall -Wextra -Werror -include $mockHeader -I $includeDirectory -c $dmaSource -o $dmaObject
if ($LASTEXITCODE -ne 0) { throw 'Circular DMA compilation failed' }
& gcc $optimization -std=c11 -Wall -Wextra -Werror -I $includeDirectory -c $attitudeSource -o $attitudeObject
if ($LASTEXITCODE -ne 0) { throw 'Attitude C compilation failed' }
& g++ $optimization -std=c++17 -Wall -Wextra -Werror -I $includeDirectory $attitudeTestSource $attitudeObject -o $attitudeExecutable
if ($LASTEXITCODE -ne 0) { throw 'Attitude test compilation failed' }
& $attitudeExecutable
if ($LASTEXITCODE -ne 0) { throw 'Attitude tests failed' }
$calibrationObject = Join-Path $outputDirectory 'imu_calibration.o'
$calibrationSource = Join-Path $projectRoot 'Core/Src/imu_calibration.c'
$calibrationTest = Join-Path $PSScriptRoot 'test_imu_calibration.cpp'
$calibrationExe = Join-Path $outputDirectory 'test_imu_calibration.exe'
& gcc $optimization -std=c11 -Wall -Wextra -Werror -I $includeDirectory -c $calibrationSource -o $calibrationObject
if ($LASTEXITCODE -ne 0) { throw 'Calibration C compilation failed' }
& g++ $optimization -std=c++17 -Wall -Wextra -Werror -I $includeDirectory $calibrationTest $calibrationObject -o $calibrationExe
if ($LASTEXITCODE -ne 0) { throw 'Calibration/filter test compilation failed' }
& $calibrationExe
if ($LASTEXITCODE -ne 0) { throw 'Calibration/filter tests failed' }
& g++ $optimization -std=c++17 -Wall -Wextra -Werror -include $mockHeader -I $includeDirectory -I $ProtocolDirectory $testSource $upstreamSource $moduleObject $attitudeObject $calibrationObject $dmaObject -o $executable
if ($LASTEXITCODE -ne 0) { throw 'Upstream interoperability test compilation failed' }
& $executable
if ($LASTEXITCODE -ne 0) { throw 'RDK UART tests failed' }

foreach ($component in @('ELRS', 'OLED', 'Odom', 'Menu')) {
    $componentSource = Join-Path $projectRoot "Core/Src/$component.c"
    $componentObject = Join-Path $outputDirectory "$component.o"
    $componentTest = Join-Path $PSScriptRoot ('test_' + $component.ToLower() + '.cpp')
    $componentExe = Join-Path $outputDirectory ('test_' + $component.ToLower() + '.exe')
    & gcc $optimization -std=c11 -Wall -Wextra -Werror -include $mockHeader -I $includeDirectory -c $componentSource -o $componentObject
    if ($LASTEXITCODE -ne 0) { throw "$component compilation failed" }
    $extraObjects = @()
    if ($component -eq 'ELRS') { $extraObjects += $dmaObject }
    & g++ $optimization -std=c++17 -Wall -Wextra -Werror -include $mockHeader -I $includeDirectory $componentTest $componentObject @extraObjects -o $componentExe
    if ($LASTEXITCODE -ne 0) { throw "$component test compilation failed" }
    & $componentExe
    if ($LASTEXITCODE -ne 0) { throw "$component tests failed" }
}

$driveObject = Join-Path $outputDirectory 'drive_control.o'
$mecanumObject = Join-Path $outputDirectory 'Mecanum.o'
& gcc $optimization -std=c11 -Wall -Wextra -Werror -include $mockHeader -I $includeDirectory -c (Join-Path $projectRoot 'Core/Src/drive_control.c') -o $driveObject
if ($LASTEXITCODE -ne 0) { throw 'Drive controller compilation failed' }
& gcc $optimization -std=c11 -Wall -Wextra -Werror -include $mockHeader -I $includeDirectory -c (Join-Path $projectRoot 'Core/Src/Mecanum.c') -o $mecanumObject
if ($LASTEXITCODE -ne 0) { throw 'Mecanum compilation failed' }
$runtimeTest = Join-Path $PSScriptRoot 'test_runtime_control.cpp'
$runtimeExe = Join-Path $outputDirectory 'test_runtime_control.exe'
$elrsObject = Join-Path $outputDirectory 'ELRS.o'
& g++ $optimization -std=c++17 -Wall -Wextra -Werror -include $mockHeader -I $includeDirectory $runtimeTest $moduleObject $attitudeObject $dmaObject $elrsObject $driveObject $mecanumObject -o $runtimeExe
if ($LASTEXITCODE -ne 0) { throw 'Runtime ownership integration compilation failed' }
& $runtimeExe
if ($LASTEXITCODE -ne 0) { throw 'Runtime ownership integration tests failed' }
foreach ($scenario in @('boot-radio', 'boot-radio-low', 'boot-remote')) {
    & $runtimeExe $scenario
    if ($LASTEXITCODE -ne 0) { throw "Runtime boot scenario failed: $scenario" }
}

# Exercise the exact vendored HAL error function; avoid maintaining a second
# copy of the implementation or mocking the branch that caused the fault.
$halSource = Join-Path $projectRoot 'Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_i2c.c'
$halCode = [System.IO.File]::ReadAllText($halSource)
$errorFunction = [regex]::Match($halCode, '(?ms)^static void I2C_ITError\(I2C_HandleTypeDef \*hi2c\)\r?\n\{.*?^\}')
if (-not $errorFunction.Success) { throw 'Could not extract actual HAL I2C error function' }
$errorInclude = Join-Path $outputDirectory 'i2c_error_under_test.inc'
[System.IO.File]::WriteAllText($errorInclude, $errorFunction.Value)
$halTest = Join-Path $PSScriptRoot 'test_i2c_hal_error.cpp'
foreach ($registered in @(0, 1)) {
    $halExe = Join-Path $outputDirectory "test_i2c_hal_error_$registered.exe"
    & g++ $optimization -std=c++17 -Wall -Wextra -Werror "-DUSE_HAL_I2C_REGISTER_CALLBACKS=$registered" -I $outputDirectory $halTest -o $halExe
    if ($LASTEXITCODE -ne 0) { throw 'Actual HAL I2C error function compilation failed' }
    & $halExe
    if ($LASTEXITCODE -ne 0) { throw 'Actual HAL I2C error function tests failed' }
}
