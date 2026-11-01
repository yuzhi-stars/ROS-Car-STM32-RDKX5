param(
    [ValidateSet('Check','Setup','Configure','Build','Flash','Reset')][string]$Action = 'Check',
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [ValidateRange(1,32)][int]$Jobs = 4,
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$configPath = Join-Path $PSScriptRoot 'local.settings.json'
$settings = if (Test-Path -LiteralPath $configPath) {
    Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
} else { [pscustomobject]@{} }

function Setting([string]$name,[string]$fallback = '') {
    $property = $settings.PSObject.Properties[$name]
    if ($null -ne $property -and -not [string]::IsNullOrWhiteSpace([string]$property.Value)) { return [string]$property.Value }
    return $fallback
}
function FullPath([string]$value) {
    $expanded = [Environment]::ExpandEnvironmentVariables($value)
    if (-not [System.IO.Path]::IsPathRooted($expanded)) { $expanded = Join-Path $projectRoot $expanded }
    return [System.IO.Path]::GetFullPath($expanded)
}
function ResolveTool([string]$configured,[string]$command) {
    if (-not [string]::IsNullOrWhiteSpace($configured)) {
        $resolved = FullPath $configured
        if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) { throw "Missing $command at $resolved. Edit tools/local.settings.json." }
        return $resolved
    }
    $found = Get-Command $command -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $found) { throw "Cannot find $command. Copy tools/local.settings.example.json to tools/local.settings.json and set its paths, or add the tool to PATH." }
    return $found.Source
}
function InvokeTool([string]$executable,[string[]]$arguments) {
    Write-Host ('> "{0}" {1}' -f $executable, ($arguments -join ' '))
    if (-not $DryRun) {
        & $executable @arguments
        if ($LASTEXITCODE -ne 0) { throw "$executable failed (exit $LASTEXITCODE)." }
    }
}
function WriteJson([string]$path,$value) {
    if ($DryRun) { Write-Host "Would generate $path"; return }
    [System.IO.File]::WriteAllText($path, ($value | ConvertTo-Json -Depth 12), [System.Text.UTF8Encoding]::new($false))
}

try {
    $armBin = Setting 'armGccBin' $env:ARM_GCC_BIN
    $gcc = ResolveTool $(if ($armBin) { Join-Path $armBin 'arm-none-eabi-gcc.exe' } else { '' }) 'arm-none-eabi-gcc.exe'
    $armBin = Split-Path -Parent $gcc
    $makeBin = Setting 'makeBin'
    $make = ResolveTool $(if ($makeBin) { Join-Path $makeBin 'mingw32-make.exe' } else { '' }) 'mingw32-make.exe'
    $cmake = ResolveTool (Setting 'cmakeExe') 'cmake.exe'
    $env:ARM_GCC_BIN = $armBin
    $env:PATH = $armBin + ';' + (Split-Path -Parent $make) + ';' + (Split-Path -Parent $cmake) + ';' + $env:PATH

    if ($Action -in @('Check','Setup','Flash','Reset')) {
        $openocd = ResolveTool (Setting 'openocdExe') 'openocd.exe'
        $scripts = Setting 'openocdScripts' $env:OPENOCD_SCRIPTS
        if (-not $scripts) {
            $openocdRoot = Split-Path -Parent (Split-Path -Parent $openocd)
            foreach ($candidate in @((Join-Path $openocdRoot 'openocd/scripts'),(Join-Path $openocdRoot 'share/openocd/scripts'))) {
                if (Test-Path -LiteralPath (Join-Path $candidate 'interface/cmsis-dap.cfg')) { $scripts = $candidate; break }
            }
        }
        if (-not $scripts) { throw 'Set openocdScripts in tools/local.settings.json.' }
        $scripts = FullPath $scripts
        foreach ($relative in @('interface/cmsis-dap.cfg','target/stm32f4x.cfg')) {
            if (-not (Test-Path -LiteralPath (Join-Path $scripts $relative) -PathType Leaf)) { throw "OpenOCD scripts are incomplete: $relative" }
        }
        $speedText = Setting 'swdSpeedKhz' '100'
        $speed = 0
        if (-not [int]::TryParse($speedText,[ref]$speed) -or $speed -lt 1 -or $speed -gt 10000) { throw 'swdSpeedKhz must be an integer from 1 to 10000.' }
    }

    if ($Action -eq 'Check') {
        Write-Host "Project: $projectRoot"
        foreach ($tool in @($cmake,$gcc,$make,$openocd)) { InvokeTool $tool @('--version') }
        $gdb = ResolveTool (Join-Path $armBin 'arm-none-eabi-gdb.exe') 'arm-none-eabi-gdb.exe'
        InvokeTool $gdb @('--version')
        Write-Host "OpenOCD scripts: $scripts"
        Write-Host "DAPLink SWD speed: $speed kHz. This check does not connect to hardware."
    }

    if ($Action -eq 'Setup') {
        $gdb = ResolveTool (Join-Path $armBin 'arm-none-eabi-gdb.exe') 'arm-none-eabi-gdb.exe'
        $vscodeDirectory = Join-Path $projectRoot '.vscode'
        if (-not $DryRun) { New-Item -ItemType Directory -Force -Path $vscodeDirectory | Out-Null }
        $launch = [ordered]@{
            version='0.2.0'; configurations=@([ordered]@{
                name='DAPLink OpenOCD: Debug ROS_car_V1'; type='cortex-debug'; request='launch'; servertype='openocd'
                serverpath=$openocd; searchDir=@($scripts); armToolchainPath=$armBin; gdbPath=$gdb
                cwd='${workspaceFolder}'; executable='${workspaceFolder}/build/Debug/ROS_car_V1.elf'; device='STM32F405RG'
                configFiles=@('${workspaceFolder}/tools/openocd/ros_car.cfg')
                openOCDPreConfigLaunchCommands=@("set ADAPTER_KHZ $speed")
                runToEntryPoint='main'; preLaunchTask='Build Debug'; postDebugTask='Reset Run DAPLink OpenOCD'
            })
        }
        $properties = [ordered]@{
            configurations=@([ordered]@{ name='STM32'; compilerPath=$gcc; intelliSenseMode='windows-gcc-arm'
                compileCommands='${workspaceFolder}/build/Debug/compile_commands.json'; cStandard='c11' }); version=4
        }
        WriteJson (Join-Path $vscodeDirectory 'launch.json') $launch
        WriteJson (Join-Path $vscodeDirectory 'c_cpp_properties.json') $properties
        Write-Host 'VS Code debug and IntelliSense paths generated. Build Debug next. F8 binding is a separate user setting.'
    }

    if ($Action -in @('Configure','Build','Flash')) {
        Push-Location $projectRoot
        try {
            InvokeTool $cmake @('--preset',$Configuration,"-DTOOLCHAIN_BIN_DIR=$($armBin.Replace('\','/'))","-DCMAKE_MAKE_PROGRAM=$($make.Replace('\','/'))")
            if ($Action -ne 'Configure') { InvokeTool $cmake @('--build','--preset',$Configuration,'--parallel',"$Jobs") }
        } finally { Pop-Location }
    }

    if ($Action -in @('Flash','Reset')) {
        $ocdArguments = @('-s',$scripts,'-c',"set ADAPTER_KHZ $speed",'-f',(Join-Path $PSScriptRoot 'openocd/ros_car.cfg'),
            '-c','gdb_port disabled','-c','tcl_port disabled','-c','telnet_port disabled')
        if ($Action -eq 'Flash') {
            $elf = Join-Path $projectRoot "build/$Configuration/ROS_car_V1.elf"
            if (-not $DryRun -and -not (Test-Path -LiteralPath $elf -PathType Leaf)) { throw "Missing firmware: $elf" }
            if ($elf -match '[{}\r\n]') { throw 'OpenOCD firmware path cannot contain braces or newlines.' }
            $ocdArguments += @('-c',('program {' + $elf.Replace('\','/') + '} verify reset exit'))
        } else { $ocdArguments += @('-c','init','-c','reset run','-c','shutdown') }
        InvokeTool $openocd $ocdArguments
    }
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
