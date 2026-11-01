param([string]$SourceRoot = (Join-Path $PSScriptRoot '..'))
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $SourceRoot).Path
$failures = [System.Collections.Generic.List[string]]::new()
$jsonFiles = @('CMakePresets.json','.vscode/tasks.json','.vscode/settings.json','.vscode/extensions.json',
    '.vscode/keybindings.example.json','tools/local.settings.example.json')
foreach ($relative in $jsonFiles) {
    try { Get-Content -LiteralPath (Join-Path $root $relative) -Raw | ConvertFrom-Json | Out-Null }
    catch { $failures.Add("JSON invalid: $relative - $_") }
}
$scripts = @()
foreach ($relative in @('tools','tests')) { $scripts += Get-ChildItem -LiteralPath (Join-Path $root $relative) -Recurse -File -Filter '*.ps1' }
foreach ($script in $scripts) {
    $tokens = $null; $parseErrors = $null
    [System.Management.Automation.Language.Parser]::ParseFile($script.FullName,[ref]$tokens,[ref]$parseErrors) | Out-Null
    foreach ($parseError in $parseErrors) { $failures.Add("PowerShell invalid: $($script.Name) - $parseError") }
}
$documents = @(Get-Item -LiteralPath (Join-Path $root 'README.md'))
$documents += Get-ChildItem -LiteralPath (Join-Path $root 'docs') -Recurse -File -Filter '*.md'
$linkCount = 0
foreach ($document in $documents) {
    $body = [System.IO.File]::ReadAllText($document.FullName)
    $fences = [regex]::Matches($body,'(?m)^```').Count
    if (($fences % 2) -ne 0) { $failures.Add("Unclosed code fence: $($document.Name)") }
    foreach ($match in [regex]::Matches($body,'\]\(([^)]+)\)')) {
        $target = $match.Groups[1].Value
        if ($target -match '^(https?://|mailto:|#)') { continue }
        $target = [Uri]::UnescapeDataString(($target -split '#')[0]).Trim('<','>')
        if ($target -and -not (Test-Path -LiteralPath (Join-Path $document.DirectoryName $target))) { $failures.Add("Broken link: $($document.Name) -> $target") }
        ++$linkCount
    }
}
if ($failures.Count -gt 0) { $failures | ForEach-Object { Write-Host $_ }; throw "$($failures.Count) material checks failed." }
Write-Host "PASS: $($jsonFiles.Count) JSON files, $($scripts.Count) PowerShell scripts, $($documents.Count) Markdown files, $linkCount relative links and code fences."
