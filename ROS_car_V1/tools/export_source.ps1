param([string]$OutputFile = 'dist/ROS_car_V1_source.zip')
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$outputPath = if ([System.IO.Path]::IsPathRooted($OutputFile)) { [System.IO.Path]::GetFullPath($OutputFile) } else { [System.IO.Path]::GetFullPath((Join-Path $projectRoot $OutputFile)) }
if (-not $outputPath.StartsWith($projectRoot + '\',[System.StringComparison]::OrdinalIgnoreCase)) { throw 'Output must stay inside the project directory.' }
# Never overwrite a source or a prior release. A new name keeps earlier packages.
if ([System.IO.Path]::GetExtension($outputPath) -ne '.zip') { throw 'OutputFile must end with .zip.' }
if (Test-Path -LiteralPath $outputPath) { throw "Package already exists: $outputPath. Choose -OutputFile with a new name." }
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $outputPath) | Out-Null
$files = @()
foreach ($relative in @('README.md','.gitignore','.clangd','CMakeLists.txt','CMakePresets.json','ROS_car_V1.ioc','STM32F405xx_FLASH.ld','startup_stm32f405xx.s')) {
    $files += Get-Item -LiteralPath (Join-Path $projectRoot $relative) -Force
}
foreach ($relative in @('Core','Drivers','cmake','docs','tests','tools','.vscode')) {
    $files += Get-ChildItem -LiteralPath (Join-Path $projectRoot $relative) -File -Recurse -Force
}
$files += Get-Item -LiteralPath (Join-Path $projectRoot 'MDK-ARM/ROS_car_V1.uvprojx')
$files += Get-Item -LiteralPath (Join-Path $projectRoot 'MDK-ARM/startup_stm32f405xx.s')
$files += Get-ChildItem -LiteralPath (Join-Path $projectRoot 'MDK-ARM/RTE') -File -Recurse -Force
$excluded = @('tools/local.settings.json','.vscode/launch.json','.vscode/c_cpp_properties.json')
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$stream = [System.IO.File]::Open($outputPath,[System.IO.FileMode]::CreateNew)
$zip = [System.IO.Compression.ZipArchive]::new($stream,[System.IO.Compression.ZipArchiveMode]::Create)
$count = 0
try {
    foreach ($file in ($files | Sort-Object FullName -Unique)) {
        $relative = $file.FullName.Substring($projectRoot.Length+1).Replace('\','/')
        if ($relative -in $excluded -or $relative -match '/__pycache__/|\.(pyc|o|obj|su|elf|map|lst|axf)$') { continue }
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$file.FullName,('ROS_car_V1/' + $relative),[System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
        ++$count
    }
} finally { $zip.Dispose(); $stream.Dispose() }
Write-Host "Exported $count source/document files to $outputPath"
Write-Host 'Excluded: build, dist, archive, editor-generated paths, local tool settings and local IDE state.'
