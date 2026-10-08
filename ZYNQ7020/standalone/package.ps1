param(
    [Parameter(Mandatory=$true)][string]$Output,
    [Parameter(Mandatory=$true)][string]$IpRepository,
    [Parameter(Mandatory=$true)][string]$Part
)
$ErrorActionPreference = 'Stop'
$Output = [IO.Path]::GetFullPath($Output)
$Root = [IO.Path]::GetFullPath("$PSScriptRoot/../..")
$Diagnostic = Join-Path $Output 'diagnostic'
$Boot = [IO.File]::ReadAllBytes("$Diagnostic/BOOT.BIN")
$Checksum = [uint64]0
for ($Offset = 0x20; $Offset -le 0x48; $Offset += 4) {
    $Checksum += [BitConverter]::ToUInt32($Boot,$Offset)
}
if ([BitConverter]::ToUInt32($Boot,0x20) -ne 0xAA995566L -or
    [BitConverter]::ToUInt32($Boot,0x24) -ne 0x584C4E58L -or
    ($Checksum -band 0xFFFFFFFFL) -ne 0xFFFFFFFFL -or
    ([uint64][BitConverter]::ToUInt32($Boot,0x30) + [BitConverter]::ToUInt32($Boot,0x34)) -gt $Boot.Length) {
    throw 'Invalid Zynq boot header'
}
$Revision = git -c safe.directory=$Root -C $Root rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Project revision unavailable' }
$Changes = git -c safe.directory=$Root -C $Root status --porcelain
if ($LASTEXITCODE -ne 0) { throw 'Project status unavailable' }
$Files = [ordered]@{}
foreach ($Name in @('diagnostic/BOOT.BIN','diagnostic/fsbl.elf','diagnostic/diagnostic.elf',
                    'hardware/system.bit','hardware/system.hdf','hardware/timing.rpt')) {
    $Path = Join-Path $Output $Name
    $Files[$Name] = [ordered]@{ bytes=(Get-Item -LiteralPath $Path).Length; sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$Manifest = [ordered]@{
    target=$Part; fitted_speed_suffix_verified=$false; tools='Vivado/SDK 2019.1'
    source_revision=$Revision.Trim(); source_dirty=[bool]$Changes
    cpu_mhz=600; ddr_mhz=400; ddr_mib=512; ddr_bus_bits=16; ddr_test_mib=464
    digilent_ip='f4613fff005b098065fd5d619a2b88e55720a423'
    timing_passed=$true; hardware_tested=$false; files=$Files
}
$Utf8 = New-Object Text.UTF8Encoding($false)
[IO.File]::WriteAllText("$Diagnostic/manifest.json", ($Manifest | ConvertTo-Json -Depth 5) + "`n", $Utf8)
$Xilinx = [IO.File]::ReadAllText("$Root/ZYNQ7020/oscill.sdk/fsbl/src/main.c")
$Notice = [regex]::Match($Xilinx,'(?s)/\*.*?\*/').Value
$Digilent = [IO.File]::ReadAllText("$IpRepository/ip/rgb2dvi/src/rgb2dvi.vhd")
$Notice = "Xilinx FSBL / standalone BSP`n$Notice`n`nDigilent rgb2dvi`n" + ($Digilent -split '(?m)^library ',2)[0]
[IO.File]::WriteAllText("$Diagnostic/THIRD_PARTY_LICENSES.txt", $Notice, $Utf8)
$Launch = [IO.File]::ReadAllText("$Root/docs/BRINGUP.md")
$Launch = ($Launch -split '## Linux и GUI',2)[0].Trim() + "`n"
[IO.File]::WriteAllText("$Diagnostic/FIRST-LAUNCH.txt", $Launch, $Utf8)
Compress-Archive -LiteralPath "$Diagnostic/BOOT.BIN","$Diagnostic/manifest.json",
    "$Diagnostic/THIRD_PARTY_LICENSES.txt","$Diagnostic/FIRST-LAUNCH.txt" -DestinationPath "$Diagnostic/oscill-ddr-hdmi-test.zip" -Force
Write-Host "Board test package: $Diagnostic/oscill-ddr-hdmi-test.zip"
