# vivado -mode batch -source simulate.tcl -tclargs /absolute/build-directory
if {$argc != 1} { error "Provide an external build directory" }
set source_dir [file dirname [file normalize [info script]]]
set output_dir [file normalize [lindex $argv 0]]
if {[string first $source_dir $output_dir] == 0} { error "Build outside the FPGA source tree" }
create_project capture_pack_test $output_dir -part xc7z020clg400-1 -force
add_files [file join $source_dir axis_capture_pack.v]
add_files -fileset sim_1 [file join $source_dir tests axis_capture_pack_tb.v]
set_property file_type SystemVerilog [get_files axis_capture_pack_tb.v]
set_property top axis_capture_pack_tb [get_filesets sim_1]
launch_simulation
run all
close_sim
close_project
