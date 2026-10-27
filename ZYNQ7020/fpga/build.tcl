# vivado -mode batch -source build.tcl -tclargs /absolute/output [part]
if {$argc < 1 || $argc > 2} { error "Expected output directory and optional part" }
set here [file dirname [file normalize [info script]]]
set root [file dirname $here]
set output [file normalize [lindex $argv 0]]
file mkdir $output
open_project $root/oscill.xpr
set part [get_property PART [current_project]]
if {$argc == 2} { set part [lindex $argv 1] }
if {![regexp {^xc7z020clg400-[123]$} $part]} { error "Expected XC7Z020 CLG400 part" }
if {[get_property PART [current_project]] ne $part} {
    set_property PART $part [current_project]
}
set_param general.maxThreads 8
update_ip_catalog
set measure [get_ips -all -filter {IPDEF == "xilinx.com:hls:record_measure:1.0"}]
if {[llength $measure] != 1} { error "Expected one record_measure IP" }
upgrade_ip $measure
set bd [get_files system.bd]
open_bd_design $bd
validate_bd_design
save_bd_design
generate_target all $bd
make_wrapper -files $bd -top
update_compile_order -fileset sources_1
# Resource and timing reports are requested interactively, not written by builds.
foreach report [get_report_configs] { set_property IS_ENABLED false $report }
set_property STEPS.POST_ROUTE_PHYS_OPT_DESIGN.IS_ENABLED true [get_runs impl_1]
set_property STEPS.POST_ROUTE_PHYS_OPT_DESIGN.ARGS.DIRECTIVE AggressiveExplore [get_runs impl_1]
reset_run synth_1
launch_runs synth_1 -jobs 8
wait_on_run synth_1
if {[get_property STATUS [get_runs synth_1]] ne "synth_design Complete!"} { error "Synthesis failed" }
launch_runs impl_1 -to_step write_bitstream -jobs 8
wait_on_run impl_1
if {[get_property STATUS [get_runs impl_1]] ne "write_bitstream Complete!"} { error "Implementation failed" }
open_run impl_1
if {![llength [get_cells -hier -filter {NAME =~ *capture_dsp*transfer_reg*}]] ||
    ![llength [get_cells -hier -filter {NAME =~ *capture_dsp*dsp_settings_reg*}]] ||
    [llength [get_pins -hier -filter {NAME =~ *capture_dsp*request_meta_reg/D || NAME =~ *capture_dsp*ack_meta_reg/D}]] != 2} {
    error "Capture DSP handshake constraints have no endpoints"
}
if {[get_property SLACK [get_timing_paths -delay_type max -max_paths 1]] < 0} { error "Setup timing failed" }
if {[get_property SLACK [get_timing_paths -delay_type min -max_paths 1]] < 0} { error "Hold timing failed" }
set bitstream $root/oscill.runs/impl_1/system_wrapper.bit
write_hwdef -force -file $output/system.hwdef
write_sysdef -force -hwdef $output/system.hwdef -bitfile $bitstream $output/system.sysdef
file copy -force $output/system.sysdef $output/system.hdf
file copy -force $bitstream $output/system.bit
file mkdir $root/oscill.sdk
file copy -force $output/system.hdf $root/oscill.sdk/system_wrapper.hdf
close_project
puts "OSCILL HARDWARE READY: $root/oscill.xpr $output/system.hdf $output/system.bit"
