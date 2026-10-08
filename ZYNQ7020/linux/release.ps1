param([string]$Output = "$PSScriptRoot/../../output")
$ErrorActionPreference = 'Stop'
$Output = [IO.Path]::GetFullPath($Output)
$Root = [IO.Path]::GetFullPath("$PSScriptRoot/../..")
$Release = Join-Path $Output 'linux'
$Names = @('BOOT.BIN','fsbl.elf','boot.scr','zImage','oscill-zynq7020.dtb','rootfs.squashfs',
           'sdcard.img','qspi.bin','oscill-zynq7020-qspi.dtb','qspi.scr',
           'qspi-boot.cmd','qspi-partitions.dtso','qspi-layout.json')
foreach ($Name in $Names) {
    if (!(Test-Path -LiteralPath "$Release/$Name" -PathType Leaf)) { throw "Missing $Release/$Name" }
}
$Layout = Get-Content -LiteralPath "$Release/qspi-layout.json" -Raw | ConvertFrom-Json
if ((Get-Item -LiteralPath "$Release/qspi.bin").Length -ne $Layout.flash_bytes -or
    (Get-FileHash -LiteralPath "$Release/qspi.bin" -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Layout.image_sha256) {
    throw 'QSPI image does not match its layout'
}
$Diagnostic = Get-Content -LiteralPath "$Output/diagnostic/manifest.json" -Raw | ConvertFrom-Json
foreach ($Name in @('diagnostic/fsbl.elf','hardware/system.bit')) {
    if ((Get-FileHash -LiteralPath "$Output/$Name" -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Diagnostic.files.$Name.sha256) {
        throw "Hardware changed since diagnostic packaging: $Name"
    }
}
if ((Get-FileHash -LiteralPath "$Release/fsbl.elf" -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Diagnostic.files.'diagnostic/fsbl.elf'.sha256) {
    throw 'Release FSBL does not match hardware manifest'
}
$Revision = git -c safe.directory=$Root -C $Root rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Project revision unavailable' }
$Changes = git -c safe.directory=$Root -C $Root status --porcelain
if ($LASTEXITCODE -ne 0) { throw 'Project status unavailable' }
$Pcb = git -c safe.directory="$Root/PCB" -C "$Root/PCB" rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'PCB revision unavailable' }
$Utf8 = New-Object Text.UTF8Encoding($false)
$Paths = git -c safe.directory=$Root -c core.quotepath=false -C $Root ls-files --cached --others --exclude-standard
if ($LASTEXITCODE -ne 0) { throw 'Project source list unavailable' }
$Sources = [ordered]@{}
foreach ($Path in $Paths | Sort-Object -Unique) {
    if (Test-Path -LiteralPath "$Root/$Path" -PathType Leaf) {
        $Sources[$Path] = (Get-FileHash -LiteralPath "$Root/$Path" -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}
[IO.File]::WriteAllText("$Release/source-files.json", ($Sources | ConvertTo-Json -Depth 3) + "`n", $Utf8)
$Names += 'source-files.json'
Copy-Item -LiteralPath "$Output/diagnostic/THIRD_PARTY_LICENSES.txt" -Destination "$Release/HARDWARE_LICENSES.txt" -Force
$Launch = ([IO.File]::ReadAllText("$Root/docs/BRINGUP.md") -split '## Linux и GUI',2)[1]
$Launch = "# Первый запуск Linux и GUI`n`nBOOT.BIN в этом комплекте запускает Linux. Отдельный тест DDR/HDMI поставляется в oscill-ddr-hdmi-test.zip.`n" + $Launch
$Flashing = ([IO.File]::ReadAllText("$Root/docs/FLASHING.md") -split '## Zynq-7020',2)[1]
$Launch += "`n## Запись QSPI`n" + ($Flashing -split '## STM32',2)[0]
# The standalone package refers to source documentation by name, not broken local links.
$Launch = [regex]::Replace($Launch,'\[([^\]]+)\]\(([^):]+\.md)\)', '$1 (документация проекта)')
[IO.File]::WriteAllText("$Release/FIRST-LAUNCH.md", $Launch, $Utf8)
$Files = [ordered]@{}
$Sums = @()
foreach ($Name in $Names + @('HARDWARE_LICENSES.txt','FIRST-LAUNCH.md')) {
    $Hash = (Get-FileHash -LiteralPath "$Release/$Name" -Algorithm SHA256).Hash.ToLowerInvariant()
    $Files[$Name] = [ordered]@{ bytes=(Get-Item -LiteralPath "$Release/$Name").Length; sha256=$Hash }
    $Sums += "$Hash  $Name"
}
$Manifest = [ordered]@{
    target=$Diagnostic.target; fitted_speed_suffix_verified=$false
    source_revision=$Revision.Trim(); source_dirty=[bool]$Changes; pcb_revision=$Pcb.Trim()
    buildroot='2025.02.12'; toolchain='Bootlin ARMv7 EABIHF glibc stable 2024.05-1 (GCC 13.3.0)'
    kernel='Xilinx 6.6.70'; kernel_commit='3c22f2aad113cbbea5c6434c697d002a82826da9'
    uboot='2024.10'; sdl='2.30.12'; app='0.1.0'; control_protocol=1; capture_abi=1; sample_format=1
    hardware=$Diagnostic
    capture_source='demo'; display='simple-framebuffer XRGB8888 1024x600'
    hardware_tested=$false; files=$Files
}
[IO.File]::WriteAllText("$Release/manifest.json", ($Manifest | ConvertTo-Json -Depth 8) + "`n", $Utf8)
$Sums += (Get-FileHash -LiteralPath "$Release/manifest.json" -Algorithm SHA256).Hash.ToLowerInvariant() + '  manifest.json'
[IO.File]::WriteAllText("$Release/SHA256SUMS", ($Sums -join "`n") + "`n", $Utf8)
$ArchiveFiles = @($Files.Keys | ForEach-Object { "$Release/$_" }) + @("$Release/manifest.json","$Release/SHA256SUMS")
Compress-Archive -LiteralPath $ArchiveFiles -DestinationPath "$Output/oscill-linux-gui.zip" -Force
Write-Host "Linux/GUI package: $Output/oscill-linux-gui.zip"
