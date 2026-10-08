param([string]$Config = "$PSScriptRoot/../output/build-environment.json")

$ErrorActionPreference = 'Stop'
$Device = [IO.Path]::GetFullPath("$PSScriptRoot/..")
$Output = "$Device/output"
if (!(Test-Path -LiteralPath $Config -PathType Leaf)) {
    throw 'Build environment missing. See docs/BUILD.md: Подготовка полной сборки Zynq.'
}
$Settings = Get-Content -LiteralPath $Config -Raw | ConvertFrom-Json
foreach ($Name in @('distribution', 'user', 'buildroot', 'linux_output', 'xilinx_root')) {
    if ($Settings.$Name -isnot [string] -or [string]::IsNullOrWhiteSpace($Settings.$Name) -or
        $Settings.$Name.Contains("`n") -or $Settings.$Name.Contains("`r")) {
        throw "Invalid build environment field: $Name"
    }
}
foreach ($Name in @('buildroot', 'linux_output')) {
    if (!$Settings.$Name.StartsWith('/')) { throw "$Name must be an absolute Linux path" }
}
$Settings.buildroot = $Settings.buildroot.TrimEnd('/')
$Settings.linux_output = $Settings.linux_output.TrimEnd('/')
if (!$Settings.buildroot -or !$Settings.linux_output) { throw 'Linux paths must not be /' }
foreach ($Tool in @('Vivado/2019.1/bin/vivado.bat', 'SDK/2019.1/bin/xsct.bat', 'SDK/2019.1/bin/bootgen.bat')) {
    if (!(Test-Path -LiteralPath "$($Settings.xilinx_root)/$Tool" -PathType Leaf)) {
        throw "Tool missing: $($Settings.xilinx_root)/$Tool"
    }
}

function Invoke-Linux {
    param([string[]]$Arguments)
    & wsl.exe --distribution $Settings.distribution --user $Settings.user --exec env `
        'PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin' @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Linux command failed: $($Arguments[0]) (exit $LASTEXITCODE)" }
}

$LinuxDevice = ((Invoke-Linux -Arguments @('wslpath', '-a', '-u', $Device.Replace('\','/'))) -join "`n").Trim()
if (!$LinuxDevice.StartsWith('/')) { throw 'Cannot resolve the project path in WSL' }
Invoke-Linux -Arguments @('grep', '-Fxq', 'export BR2_VERSION := 2025.02.12', "$($Settings.buildroot)/Makefile")
Invoke-Linux -Arguments @('which', 'make', 'cmake', 'python3', 'mkdosfs', 'mcopy')

Write-Host '[1/5] FPGA and FSBL'
& "$Device/build.ps1" -Output $Output -XilinxRoot $Settings.xilinx_root

Write-Host '[2/5] Linux and GUI'
Invoke-Linux -Arguments @('sh', "$LinuxDevice/linux/build.sh", $Settings.buildroot, $Settings.linux_output)

Write-Host '[3/5] BOOT.BIN'
$Images = "\\wsl.localhost\$($Settings.distribution)$($Settings.linux_output.Replace('/','\'))\images"
& "$PSScriptRoot/package.ps1" -Images $Images -Output $Output -XilinxRoot $Settings.xilinx_root

Write-Host '[4/5] SD and QSPI images'
Invoke-Linux -Arguments @('make', '-C', $Settings.buildroot, "O=$($Settings.linux_output)", 'host-genimage')
$LinuxRelease = "$LinuxDevice/output/linux"
Invoke-Linux -Arguments @('env', "PATH=$($Settings.linux_output)/host/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
    'sh', "$LinuxDevice/linux/assemble-sd.sh", "$($Settings.linux_output)/images", "$LinuxRelease/BOOT.BIN", $LinuxRelease)
Invoke-Linux -Arguments @('python3', "$LinuxDevice/linux/assemble-qspi.py", $LinuxRelease)
Invoke-Linux -Arguments @('python3', "$LinuxDevice/linux/tests/verify_release.py", $LinuxRelease)

Write-Host '[5/5] Release archive'
& "$PSScriptRoot/release.ps1" -Output $Output
Write-Host "Ready: $Output/linux/qspi.bin"
Write-Host "Ready: $Output/linux/sdcard.img"
Write-Host "Ready: $Output/linux/oscill-linux-gui.zip"
