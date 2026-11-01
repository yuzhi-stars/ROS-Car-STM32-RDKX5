# Load the local native MinGW path only in this test process.
$nativeProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$nativeConfigPath = Join-Path $nativeProjectRoot 'tools/local.settings.json'
if (Test-Path -LiteralPath $nativeConfigPath) {
    $nativeSettings = Get-Content -LiteralPath $nativeConfigPath -Raw | ConvertFrom-Json
    if ($nativeSettings.PSObject.Properties['makeBin'] -and $nativeSettings.makeBin) {
        $env:PATH = [string]$nativeSettings.makeBin + ';' + $env:PATH
    }
}
foreach ($nativeCommand in @('gcc.exe','g++.exe')) {
    if (-not (Get-Command $nativeCommand -CommandType Application -ErrorAction SilentlyContinue)) {
        throw "Native test compiler missing: $nativeCommand. Set makeBin to a MinGW bin with gcc/g++, or add it to PATH."
    }
}
