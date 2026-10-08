param(
    [string]$Output = "$PSScriptRoot/../output",
    [string]$XilinxRoot = 'C:/Xilinx',
    [ValidateSet('xc7z020clg400-1','xc7z020clg400-2','xc7z020clg400-3')]
    [string]$Part = 'xc7z020clg400-1'
)
$ErrorActionPreference = 'Stop'
$Output = [IO.Path]::GetFullPath($Output).Replace('\','/')
$Vivado = "$XilinxRoot/Vivado/2019.1/bin/vivado.bat"
$Xsct = "$XilinxRoot/SDK/2019.1/bin/xsct.bat"
$Bootgen = "$XilinxRoot/SDK/2019.1/bin/bootgen.bat"
foreach ($Tool in @($Vivado,$Xsct,$Bootgen)) {
    if (!(Test-Path -LiteralPath $Tool)) { throw "Tool missing: $Tool" }
}
New-Item -ItemType Directory -Force $Output | Out-Null
$Dependency = "$Output/dependencies/vivado-library"
$Revision = 'f4613fff005b098065fd5d619a2b88e55720a423'
if (!(Test-Path -LiteralPath "$Dependency/.git")) {
    git clone --no-checkout https://github.com/Digilent/vivado-library.git $Dependency
    if ($LASTEXITCODE -ne 0) { throw 'Digilent IP download failed' }
    git -C $Dependency checkout --detach $Revision
    if ($LASTEXITCODE -ne 0) { throw 'Digilent IP checkout failed' }
}
$Actual = git -C $Dependency rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $Actual.Trim() -ne $Revision) { throw 'Digilent IP revision mismatch' }
Push-Location $Output
try {
    & $Vivado -mode batch -source "$PSScriptRoot/fpga/build.tcl" -tclargs "$Output/hardware" $Dependency $Part
    if ($LASTEXITCODE -ne 0) { throw 'Vivado build failed' }
    & $Xsct "$PSScriptRoot/standalone/build.tcl" "$Output/hardware" "$Output/diagnostic"
    if ($LASTEXITCODE -ne 0) { throw 'Standalone build failed' }
    & $Bootgen -arch zynq -image "$Output/diagnostic/diagnostic.bif" -o "$Output/diagnostic/BOOT.BIN" -w on
    if ($LASTEXITCODE -ne 0) { throw 'Boot image creation failed' }
    & "$PSScriptRoot/standalone/package.ps1" -Output $Output -IpRepository $Dependency -Part $Part
    Write-Host "SD test firmware: $Output/diagnostic/BOOT.BIN"
} finally { Pop-Location }
