param(
    [Parameter(Mandatory=$true)][string]$Images,
    [string]$Output = "$PSScriptRoot/../output",
    [string]$XilinxRoot = 'C:/Xilinx'
)
$ErrorActionPreference = 'Stop'
$Images = [IO.Path]::GetFullPath($Images).Replace('\','/')
$Output = [IO.Path]::GetFullPath($Output).Replace('\','/')
$Bootgen = "$XilinxRoot/SDK/2019.1/bin/bootgen.bat"
foreach ($File in @("$Output/linux/fsbl.elf", "$Output/hardware/system.bit", "$Images/u-boot.elf",
                    "$Images/image.bin")) {
    if (!(Test-Path -LiteralPath $File)) { throw "Missing $File" }
}
if ((Get-Item -LiteralPath "$Images/image.bin").Length -gt 0x9e0000) { throw 'image.bin exceeds the QSPI partition' }
New-Item -ItemType Directory -Force "$Output/linux", "$Output/logs" | Out-Null
@"
image: {
  [bootloader] "$Output/linux/fsbl.elf"
  "$Output/hardware/system.bit"
  "$Images/u-boot.elf"
}
"@ | Set-Content -Encoding ascii "$Output/logs/linux.bif"
& $Bootgen -arch zynq -image "$Output/logs/linux.bif" -o "$Output/linux/BOOT.BIN" -w on
if ($LASTEXITCODE -ne 0) { throw 'Linux boot image creation failed' }
if ((Get-Item -LiteralPath "$Output/linux/BOOT.BIN").Length -gt 0x500000) { throw 'BOOT.BIN exceeds the QSPI partition' }
Copy-Item -LiteralPath "$Images/image.bin" -Destination "$Output/linux/image.bin" -Force
Write-Host "Linux boot files: $Output/linux"
