# vivado_hls -f synthesize_measure.tcl -tclargs /absolute/build-directory [part] [verify]
# The 2019.1 launcher removes -tclargs but retains -f and the script in argv.
if {$argc < 3 || $argc > 5 || [lindex $argv 0] ne "-f"} {
    puts stderr "Provide -f script -tclargs and an external HLS build directory";exit 1
}
set here [file dirname [file normalize [info script]]]
set output [file normalize [lindex $argv 2]]
set part xc7z020clg400-1
if {$argc >= 4} { set part [lindex $argv 3] }
if {![regexp {^xc7z020clg400-[123]$} $part]} { puts stderr "Expected XC7Z020 CLG400 part";exit 1 }
set verify [expr {$argc == 5}]
if {$verify && [lindex $argv 4] ne "verify"} { puts stderr "Expected verify as the last argument";exit 1 }
if {[string first $here $output] == 0} { puts stderr "Build outside the FPGA source tree";exit 1 }
set common [file normalize $here/../gui/src]
proc synthesize_measure {here output common part verify} {
file mkdir $output
file delete -force $output/ready
cd $output
open_project -reset record_measure
set_top record_measure
foreach source [list $here/record_measure.c $here/record_trace.c $common/signal_measure.c] {
    add_files $source -cflags "-I$common -I$here -DSIGNAL_NUMBER_FLOAT -DSIGNAL_TRACE_REPLAY"
}
add_files -tb $here/tests/record_measure_tb.c -cflags "-I$common -DSIGNAL_NUMBER_FLOAT"
open_solution -reset solution
set_part $part
create_clock -period 10 -name default
config_interface -m_axi_addr64=false
set_directive_inline signal_edge_advance
set_directive_inline signal_edge_fraction
set_directive_inline value_at
set_directive_inline code_at
set_directive_inline record_trace_begin
set_directive_inline record_trace_prepare
set_directive_inline prepare
set_directive_inline record_trace_code
set_directive_inline record_trace_value
set_directive_inline signal_measure_amplitude
set_directive_inline signal_measure_timing
foreach operation {fadd fsub fmul fdiv fcmp} {
    set_directive_allocation -type operation -limit 1 record_measure $operation
}
csim_design
csynth_design
if {$verify} { cosim_design -rtl verilog -tool xsim -argv rtl }
source $here/package_hls.tcl
package_hls_ip $output/record_measure/solution
close_project
set ready [open $output/ready w];puts $ready $part;close $ready
puts "OSCILL MEASUREMENT IP READY: $output/record_measure/solution/impl/ip"
}
if {[catch {synthesize_measure $here $output $common $part $verify} message options]} {
    puts stderr $message
    if {[dict exists $options -errorinfo]} { puts stderr [dict get $options -errorinfo] }
    exit 1
}
exit 0
