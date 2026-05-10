param(
    [string]$Version = "3.14.3"
)

$ErrorActionPreference = "Stop"

$runtimeRoot = $PSScriptRoot
$archiveName = "python-$Version-embed-amd64.zip"
$downloadUrl = "https://www.python.org/ftp/python/$Version/$archiveName"
$targetDirName = "Python-$Version-embed-amd64"
$targetDir = Join-Path $runtimeRoot $targetDirName
$tempArchivePath = Join-Path ([System.IO.Path]::GetTempPath()) $archiveName

Write-Host "Downloading $downloadUrl"
Invoke-WebRequest -Uri $downloadUrl -OutFile $tempArchivePath

if (Test-Path -LiteralPath $targetDir) {
    Write-Host "Removing old runtime directory: $targetDir"
    Remove-Item -LiteralPath $targetDir -Recurse -Force
}

Write-Host "Extracting to $targetDir"
Expand-Archive -LiteralPath $tempArchivePath -DestinationPath $targetDir -Force
Remove-Item -LiteralPath $tempArchivePath -Force

$pythonExe = Join-Path $targetDir "python.exe"
if (-not (Test-Path -LiteralPath $pythonExe)) {
    throw "Embedded Python setup failed: python.exe not found in $targetDir"
}

$pthFile = Get-ChildItem -LiteralPath $targetDir -Filter "python*._pth" | Select-Object -First 1
if ($null -eq $pthFile) {
    throw "Embedded Python setup failed: python*._pth not found in $targetDir"
}

$scriptsPathEntry = "..\..\Scripts"
$legacyScriptsPathEntry = "..\\..\\Scripts"
$pthLines = Get-Content -LiteralPath $pthFile.FullName
$newPthLines = New-Object System.Collections.Generic.List[string]
$inserted = $false
$changed = $false

foreach ($line in $pthLines) {
    $normalizedLine = if ($line -eq $legacyScriptsPathEntry) {
        $changed = $true
        $scriptsPathEntry
    } else {
        $line
    }

    $newPthLines.Add($normalizedLine)
    if ($normalizedLine -eq $scriptsPathEntry) {
        $inserted = $true
    }

    if (-not $inserted -and $normalizedLine -eq ".") {
        $newPthLines.Add($scriptsPathEntry)
        $inserted = $true
        $changed = $true
    }
}

if (-not $inserted) {
    $newPthLines.Add($scriptsPathEntry)
    $changed = $true
}

if ($changed) {
    Set-Content -LiteralPath $pthFile.FullName -Value $newPthLines -Encoding Ascii
}

Write-Host "Embedded Python is ready: $pythonExe"
