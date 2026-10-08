param(
    [Parameter(Mandatory=$true)][string]$Images,
    [string]$Output = "$PSScriptRoot/../../output",
    [string]$XilinxRoot = 'C:/Xilinx'
)
$ErrorActionPreference = 'Stop'
$Images = [IO.Path]::GetFullPath($Images).Replace('\','/')
$Output = [IO.Path]::GetFullPath($Output).Replace('\','/')
$Bootgen = "$XilinxRoot/SDK/2019.1/bin/bootgen.bat"
foreach ($File in @("$Output/diagnostic/fsbl.elf", "$Output/hardware/system.bit", "$Images/u-boot.elf",
                    "$Images/boot.scr", "$Images/zImage", "$Images/oscill-zynq7020.dtb", "$Images/rootfs.squashfs")) {
    if (!(Test-Path -LiteralPath $File)) { throw "Missing $File" }
}
New-Item -ItemType Directory -Force "$Output/linux" | Out-Null
@"
image: {
  [bootloader] "$Output/diagnostic/fsbl.elf"
  "$Output/hardware/system.bit"
  "$Images/u-boot.elf"
}
"@ | Set-Content -Encoding ascii "$Output/linux/linux.bif"
& $Bootgen -arch zynq -image "$Output/linux/linux.bif" -o "$Output/linux/BOOT.BIN" -w on
if ($LASTEXITCODE -ne 0) { throw 'Linux boot image creation failed' }
foreach ($Name in @('boot.scr','zImage','oscill-zynq7020.dtb','rootfs.squashfs')) {
    Copy-Item -LiteralPath "$Images/$Name" -Destination "$Output/linux/$Name" -Force
}
Copy-Item -LiteralPath "$Output/diagnostic/fsbl.elf" -Destination "$Output/linux/fsbl.elf" -Force
Write-Host "Linux boot files: $Output/linux"
