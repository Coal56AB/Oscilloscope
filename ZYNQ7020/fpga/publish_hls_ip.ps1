param([Parameter(Mandatory=$true)][string]$Archive)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$Destination = [IO.Path]::GetFullPath("$PSScriptRoot/ip/record_measure")
$Package = [IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($Archive))
try {
    if (!$Package.GetEntry('component.xml')) { throw 'HLS package has no component.xml' }
    $Expected = @{}
    foreach ($Entry in $Package.Entries) {
        if ($Entry.FullName.EndsWith('/')) { continue }
        $Target = [IO.Path]::GetFullPath((Join-Path $Destination $Entry.FullName))
        if (!$Target.StartsWith($Destination + [IO.Path]::DirectorySeparatorChar)) {
            throw "HLS package path is outside its IP directory: $($Entry.FullName)"
        }
        $Expected[$Target] = $true
    }
    foreach ($Entry in $Package.Entries) {
        if ($Entry.FullName.EndsWith('/')) { continue }
        $Target = [IO.Path]::GetFullPath((Join-Path $Destination $Entry.FullName))
        New-Item -ItemType Directory -Force ([IO.Path]::GetDirectoryName($Target)) | Out-Null
        [IO.Compression.ZipFileExtensions]::ExtractToFile($Entry, $Target, $true)
    }
    Get-ChildItem -LiteralPath $Destination -File -Recurse | ForEach-Object {
        if (!$Expected.ContainsKey($_.FullName)) { Remove-Item -LiteralPath $_.FullName }
    }
} finally { $Package.Dispose() }
