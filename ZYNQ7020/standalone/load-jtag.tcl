# xsct load-jtag.tcl /absolute/output
if {$argc != 1} { error "Expected output directory from ZYNQ7020/build.ps1" }
set output [file normalize [lindex $argv 0]]
set root [file dirname [file dirname [file normalize [info script]]]]
connect
targets -set -filter {name =~ "ARM*#0"}
rst -system
source $root/oscill.sdk/hardware/ps7_init.tcl
ps7_init
fpga -file $output/hardware/system.bit
ps7_post_config
dow $output/diagnostic/diagnostic.elf
con
