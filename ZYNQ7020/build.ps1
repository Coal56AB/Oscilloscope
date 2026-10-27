param(
    [string]$Output = "$PSScriptRoot/output",
    [string]$XilinxRoot = 'C:/Xilinx',
    [ValidateSet('xc7z020clg400-1','xc7z020clg400-2','xc7z020clg400-3')]
    [string]$Part
)
$ErrorActionPreference = 'Stop'
if (!$Part) {
    [xml]$Project = Get-Content -LiteralPath "$PSScriptRoot/oscill.xpr" -Raw
    $Part = ($Project.Project.Configuration.Option | Where-Object Name -eq 'Part').Val
    if ($Part -notmatch '^xc7z020clg400-[123]$') { throw "Unsupported project part: $Part" }
}
$Output = [IO.Path]::GetFullPath($Output).Replace('\','/')
$Vivado = "$XilinxRoot/Vivado/2019.1/bin/vivado.bat"
$Hls = "$XilinxRoot/Vivado/2019.1/bin/vivado_hls.bat"
$Xsct = "$XilinxRoot/SDK/2019.1/bin/xsct.bat"
$Bootgen = "$XilinxRoot/SDK/2019.1/bin/bootgen.bat"
foreach ($Tool in @($Vivado,$Hls,$Xsct,$Bootgen)) {
    if (!(Test-Path -LiteralPath $Tool)) { throw "Tool missing: $Tool" }
}
New-Item -ItemType Directory -Force $Output | Out-Null
New-Item -ItemType Directory -Force "$Output/logs" | Out-Null
Push-Location $Output
try {
    $MeasureIp = "$Output/hls-measure/record_measure/solution/impl/ip/component.xml"
    $MeasureReady = "$Output/hls-measure/ready"
    if (Test-Path -LiteralPath $MeasureReady) { Remove-Item -LiteralPath $MeasureReady -Force }
    & $Hls -f "$PSScriptRoot/fpga/synthesize_measure.tcl" -tclargs "$Output/hls-measure" $Part
    if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $MeasureReady) -or
        !(Test-Path -LiteralPath $MeasureIp)) { throw 'Measurement IP build failed' }
    & "$PSScriptRoot/fpga/publish_hls_ip.ps1" -Archive "$Output/hls-measure/record_measure/solution/impl/ip/xilinx_com_hls_record_measure_1_0.zip"
    & $Vivado -mode batch -log "$Output/logs/vivado.log" -journal "$Output/logs/vivado.jou" -source "$PSScriptRoot/fpga/build.tcl" -tclargs "$Output/hardware" $Part
    if ($LASTEXITCODE -ne 0) { throw 'Vivado build failed' }
    $StandaloneFiles = @('linux/fsbl.elf', 'diagnostic/diagnostic.elf', 'diagnostic/diagnostic.bif')
    foreach ($Name in $StandaloneFiles) {
        if (Test-Path -LiteralPath "$Output/$Name") {
            Remove-Item -LiteralPath "$Output/$Name" -Force
        }
    }
    & $Xsct "$PSScriptRoot/standalone/build.tcl" "$Output/hardware" $Output
    if ($LASTEXITCODE -ne 0) { throw 'Standalone build failed' }
    # SDK 2019.1 xsct.bat can return zero even when its Tcl script fails.
    foreach ($Name in $StandaloneFiles) {
        if (!(Test-Path -LiteralPath "$Output/$Name" -PathType Leaf) -or
            (Get-Item -LiteralPath "$Output/$Name").Length -eq 0) {
            throw "Standalone build did not produce $Name; check SDK output"
        }
    }
    & $Bootgen -arch zynq -image "$Output/diagnostic/diagnostic.bif" -o "$Output/diagnostic/BOOT.BIN" -w on
    if ($LASTEXITCODE -ne 0) { throw 'Boot image creation failed' }
    Write-Host "SD test firmware: $Output/diagnostic/BOOT.BIN"
    Write-Host "Vivado project: $PSScriptRoot/oscill.xpr"
    Write-Host "SDK workspace: $PSScriptRoot/oscill.sdk"
} finally { Pop-Location }
