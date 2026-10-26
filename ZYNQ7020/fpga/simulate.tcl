# vivado -mode batch -source simulate.tcl -tclargs /absolute/build-directory
if {$argc != 1} { error "Provide an external build directory" }
set source_dir [file dirname [file normalize [info script]]]
set output_dir [file normalize [lindex $argv 0]]
if {[string first $source_dir $output_dir] == 0} { error "Build outside the FPGA source tree" }
create_project capture_pack_test $output_dir -part xc7z020clg400-1 -force
create_ip -name axis_dwidth_converter -vendor xilinx.com -library ip -version 1.1 -module_name adc_width_test
set_property -dict {CONFIG.S_TDATA_NUM_BYTES 4 CONFIG.M_TDATA_NUM_BYTES 8} [get_ips adc_width_test]
generate_target simulation [get_ips adc_width_test]
create_ip -name axis_data_fifo -vendor xilinx.com -library ip -version 2.0 -module_name adc_fifo_test
set_property -dict {CONFIG.TDATA_NUM_BYTES 8 CONFIG.FIFO_DEPTH 2048 CONFIG.FIFO_MODE 1 CONFIG.HAS_TLAST 0} [get_ips adc_fifo_test]
create_ip -name axis_clock_converter -vendor xilinx.com -library ip -version 1.1 -module_name adc_cdc_test
set_property -dict {CONFIG.TDATA_NUM_BYTES 8 CONFIG.HAS_TKEEP 1 CONFIG.HAS_TLAST 1 CONFIG.IS_ACLK_ASYNC 1} [get_ips adc_cdc_test]
generate_target simulation [get_ips {adc_fifo_test adc_cdc_test}]
add_files [file join $source_dir axis_capture_pack.v]
foreach source {axis_capture_frame.v capture_window_stats.v capture_trigger_probe.v
    signal_wiener.v signal_mean.v signal_boxcar.v signal_edges.v signal_trigger.v
    ad9288_input.v axis_capture_decimate.v capture_config.v} {
    add_files [file join $source_dir $source]
}
set tops {axis_capture_pack_tb axis_capture_frame_tb axis_capture_bounds_tb
    capture_window_stats_tb capture_trigger_probe_tb signal_wiener_tb
    signal_boxcar_tb signal_edges_tb signal_trigger_tb capture_window_header_tb
    axis_capture_stream_tb ad9288_input_tb adc_stream_ip_tb axis_capture_decimate_tb
    capture_config_tb ad9288_clock_tb ad9288_settle_tb}
foreach top $tops {
    add_files -fileset sim_1 [file join $source_dir tests ${top}.v]
    set_property file_type SystemVerilog [get_files ${top}.v]
}
foreach top $tops {
    set_property top $top [get_filesets sim_1]
    launch_simulation
    run all
    if {[get_value /$top/passed] != 1} { error "$top failed" }
    close_sim
}
close_project
