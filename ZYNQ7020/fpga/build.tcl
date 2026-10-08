# vivado -mode batch -source build.tcl -tclargs /absolute/output /absolute/vivado-library [part]
if {$argc < 2 || $argc > 3} { error "Expected output directory, Digilent IP repository, optional part" }
set here [file dirname [file normalize [info script]]]
set root [file dirname $here]
set output [file normalize [lindex $argv 0]]
set ip_repo [file normalize [lindex $argv 1]]
set part xc7z020clg400-1
if {$argc == 3} { set part [lindex $argv 2] }
if {![regexp {^xc7z020clg400-[123]$} $part]} { error "Expected XC7Z020 CLG400 part" }
if {![file exists $ip_repo/ip/rgb2dvi/component.xml]} { error "Digilent rgb2dvi missing" }
file mkdir $output
cd $output
create_project oscill $root -part $part -force
set_property ip_repo_paths [list $ip_repo/ip/rgb2dvi $ip_repo/if] [current_project]
set_param general.maxThreads 8
update_ip_catalog
source $here/board.tcl
create_bd_design system
proc cell {name vlnv properties} {
    set c [create_bd_cell -type ip -vlnv $vlnv $name]
    if {[llength $properties]} { set_property -dict $properties $c }
    return $c
}
proc net {args} {
    set pins {}
    foreach pin $args { lappend pins [get_bd_pins $pin] }
    connect_bd_net {*}$pins
}
proc bus {a b} { connect_bd_intf_net [get_bd_intf_pins $a] [get_bd_intf_pins $b] }
set ps [cell ps7 xilinx.com:ip:processing_system7:5.5 {}]
configure_board $ps
apply_bd_automation -rule xilinx.com:bd_rule:processing_system7 \
    -config {make_external "FIXED_IO, DDR" apply_board_preset "0" Master "Disable" Slave "Disable"} $ps
cell control xilinx.com:ip:smartconnect:1.0 {CONFIG.NUM_MI 2 CONFIG.NUM_SI 1}
cell memory xilinx.com:ip:smartconnect:1.0 {CONFIG.NUM_MI 1 CONFIG.NUM_SI 1}
cell reset_axi xilinx.com:ip:proc_sys_reset:5.0 {}
cell reset_pixel xilinx.com:ip:proc_sys_reset:5.0 {}
cell pixel_clock xilinx.com:ip:clk_wiz:6.0 {CONFIG.PRIM_IN_FREQ 100 CONFIG.NUM_OUT_CLKS 2 \
    CONFIG.CLKOUT1_REQUESTED_OUT_FREQ 50 CONFIG.CLKOUT2_USED true \
    CONFIG.CLKOUT2_REQUESTED_OUT_FREQ 250 CONFIG.RESET_TYPE ACTIVE_LOW}
cell vdma xilinx.com:ip:axi_vdma:6.3 {CONFIG.c_include_s2mm 0 CONFIG.c_include_mm2s 1 \
    CONFIG.c_num_fstores 1 CONFIG.c_m_axis_mm2s_tdata_width 32 \
    CONFIG.c_m_axi_mm2s_data_width 64 CONFIG.c_mm2s_genlock_mode 0 \
    CONFIG.c_mm2s_linebuffer_depth 8192 CONFIG.c_include_mm2s_dre 0}
cell video_cdc xilinx.com:ip:axis_clock_converter:1.1 {CONFIG.TDATA_NUM_BYTES 4 \
    CONFIG.TUSER_WIDTH 1 CONFIG.HAS_TLAST 1 CONFIG.IS_ACLK_ASYNC 1}
cell pixels xilinx.com:ip:axis_subset_converter:1.1 {CONFIG.S_TDATA_NUM_BYTES 4 \
    CONFIG.M_TDATA_NUM_BYTES 3 CONFIG.TDATA_REMAP {tdata[23:16],tdata[7:0],tdata[15:8]} \
    CONFIG.S_TUSER_WIDTH 1 CONFIG.M_TUSER_WIDTH 1 CONFIG.S_HAS_TLAST 1 CONFIG.M_HAS_TLAST 1}
cell video xilinx.com:ip:v_axi4s_vid_out:4.0 {}
cell timing xilinx.com:ip:v_tc:6.1 {CONFIG.enable_detection false CONFIG.enable_generation true \
    CONFIG.GEN_HACTIVE_SIZE 1024 CONFIG.GEN_HFRAME_SIZE 1344 \
    CONFIG.GEN_HSYNC_START 1048 CONFIG.GEN_HSYNC_END 1184 \
    CONFIG.GEN_VACTIVE_SIZE 600 CONFIG.GEN_F0_VFRAME_SIZE 620 \
    CONFIG.GEN_F0_VSYNC_VSTART 601 CONFIG.GEN_F0_VSYNC_VEND 605 \
    CONFIG.GEN_HSYNC_POLARITY Low CONFIG.GEN_VSYNC_POLARITY Low}
cell tmds digilentinc.com:ip:rgb2dvi:1.4 {CONFIG.kGenerateSerialClk false \
    CONFIG.kRstActiveHigh false CONFIG.kClkRange 1}
cell one xilinx.com:ip:xlconstant:1.1 {CONFIG.CONST_WIDTH 1 CONFIG.CONST_VAL 1}
cell zero xilinx.com:ip:xlconstant:1.1 {CONFIG.CONST_WIDTH 1 CONFIG.CONST_VAL 0}
cell interrupts xilinx.com:ip:xlconcat:2.1 {CONFIG.NUM_PORTS 1}
bus ps7/M_AXI_GP0 control/S00_AXI
bus control/M00_AXI vdma/S_AXI_LITE
bus control/M01_AXI timing/ctrl
bus vdma/M_AXI_MM2S memory/S00_AXI
bus memory/M00_AXI ps7/S_AXI_HP2
bus vdma/M_AXIS_MM2S video_cdc/S_AXIS
bus video_cdc/M_AXIS pixels/S_AXIS
bus pixels/M_AXIS video/video_in
bus timing/vtiming_out video/vtiming_in
bus video/vid_io_out tmds/RGB
net ps7/FCLK_CLK0 ps7/M_AXI_GP0_ACLK ps7/S_AXI_HP2_ACLK control/aclk memory/aclk \
    reset_axi/slowest_sync_clk pixel_clock/clk_in1 vdma/s_axi_lite_aclk \
    vdma/m_axi_mm2s_aclk vdma/m_axis_mm2s_aclk video_cdc/s_axis_aclk timing/s_axi_aclk
net ps7/FCLK_RESET0_N reset_axi/ext_reset_in reset_pixel/ext_reset_in pixel_clock/resetn
net reset_axi/peripheral_aresetn control/aresetn memory/aresetn vdma/axi_resetn video_cdc/s_axis_aresetn timing/s_axi_aresetn
net pixel_clock/clk_out1 reset_pixel/slowest_sync_clk video_cdc/m_axis_aclk \
    pixels/aclk video/aclk timing/clk tmds/PixelClk
net pixel_clock/clk_out2 tmds/SerialClk
net pixel_clock/locked reset_pixel/dcm_locked
net reset_pixel/peripheral_aresetn video_cdc/m_axis_aresetn pixels/aresetn video/aresetn timing/resetn tmds/aRst_n
net zero/dout video/fid
net one/dout video/aclken video/vid_io_out_ce timing/clken timing/s_axi_aclken
net video/vtg_ce timing/gen_clken
net vdma/mm2s_introut interrupts/In0
net interrupts/dout ps7/IRQ_F2P
foreach {port pin width} {hdmi_clk_p TMDS_Clk_p 1 hdmi_clk_n TMDS_Clk_n 1 \
                         hdmi_data_p TMDS_Data_p 3 hdmi_data_n TMDS_Data_n 3} {
    if {$width == 1} { set p [create_bd_port -dir O $port] } \
    else { set p [create_bd_port -dir O -from 2 -to 0 $port] }
    connect_bd_net $p [get_bd_pins tmds/$pin]
}
connect_bd_net [create_bd_port -dir O hdmi_enable] [get_bd_pins one/dout]
assign_bd_address
set_property offset 0x43000000 [get_bd_addr_segs ps7/Data/SEG_vdma_Reg]
set_property offset 0x43C00000 [get_bd_addr_segs ps7/Data/SEG_timing_Reg]
validate_bd_design
save_bd_design
set bd [get_files system.bd]
generate_target all $bd
add_files -norecurse [make_wrapper -files $bd -top]
add_files -fileset constrs_1 $here/hdmi.xdc
set_property top system_wrapper [current_fileset]
launch_runs synth_1 -jobs 8
wait_on_run synth_1
if {[get_property STATUS [get_runs synth_1]] ne "synth_design Complete!"} { error "Synthesis failed" }
launch_runs impl_1 -to_step write_bitstream -jobs 8
wait_on_run impl_1
if {[get_property STATUS [get_runs impl_1]] ne "write_bitstream Complete!"} { error "Implementation failed" }
open_run impl_1
report_timing_summary -file $output/timing.rpt
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
