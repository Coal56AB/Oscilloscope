# AD9288 receive path, independent of the HDMI HP2 reader.
cell adc_clock xilinx.com:ip:clk_wiz:6.0 {CONFIG.PRIM_IN_FREQ 100 CONFIG.NUM_OUT_CLKS 2 \
    CONFIG.CLKOUT1_REQUESTED_OUT_FREQ 100 CONFIG.CLKOUT2_USED true \
    CONFIG.CLKOUT2_REQUESTED_OUT_FREQ 100 CONFIG.CLKOUT2_REQUESTED_PHASE 148.5 \
    CONFIG.RESET_TYPE ACTIVE_LOW}
cell reset_adc xilinx.com:ip:proc_sys_reset:5.0 {}
cell capture_gpio xilinx.com:ip:axi_gpio:2.0 {CONFIG.C_IS_DUAL 1 CONFIG.C_GPIO_WIDTH 32 \
    CONFIG.C_ALL_OUTPUTS 1 CONFIG.C_DOUT_DEFAULT 0x00003000 \
    CONFIG.C_GPIO2_WIDTH 32 CONFIG.C_ALL_INPUTS_2 1}
create_bd_cell -type module -reference capture_config capture_config
create_bd_cell -type module -reference capture_dsp_config capture_dsp
create_bd_cell -type module -reference axis_capture_decimate capture_decimate
create_bd_cell -type module -reference ad9288_input adc
create_bd_cell -type module -reference axis_capture_frame capture_frame
create_bd_cell -type module -reference capture_status capture_status
set_property CONFIG.PHASE 148.5 [get_bd_intf_pins adc/m_axis]
set_property CONFIG.PHASE 148.5 [get_bd_intf_pins capture_decimate/s_axis]
set_property CONFIG.PHASE 148.5 [get_bd_intf_pins capture_decimate/m_axis]
set_property CONFIG.PHASE 148.5 [get_bd_intf_pins capture_frame/s_axis]
set_property CONFIG.PHASE 148.5 [get_bd_intf_pins capture_frame/m_axis]
cell capture_width xilinx.com:ip:axis_dwidth_converter:1.1 {CONFIG.S_TDATA_NUM_BYTES 4 \
    CONFIG.M_TDATA_NUM_BYTES 8}
cell capture_fifo xilinx.com:ip:axis_data_fifo:2.0 {CONFIG.TDATA_NUM_BYTES 8 \
    CONFIG.FIFO_DEPTH 2048 CONFIG.FIFO_MODE 1 CONFIG.HAS_TLAST 0}
cell capture_cdc xilinx.com:ip:axis_clock_converter:1.1 {CONFIG.TDATA_NUM_BYTES 8 \
    CONFIG.HAS_TKEEP 1 CONFIG.HAS_TLAST 1 CONFIG.IS_ACLK_ASYNC 1}
cell capture_dma xilinx.com:ip:axi_dma:7.1 {CONFIG.c_include_mm2s 0 \
    CONFIG.c_include_s2mm 1 CONFIG.c_include_sg 1 CONFIG.c_sg_length_width 23 \
    CONFIG.c_sg_include_stscntrl_strm 0 \
    CONFIG.c_m_axi_s2mm_data_width 64 \
    CONFIG.c_s2mm_burst_size 64 CONFIG.c_include_s2mm_dre 0}
cell capture_memory xilinx.com:ip:smartconnect:1.0 {CONFIG.NUM_MI 1 CONFIG.NUM_SI 2}
bus control/M03_AXI capture_gpio/S_AXI
bus control/M04_AXI capture_dma/S_AXI_LITE
bus control/M05_AXI capture_dsp/s_axi
bus capture_dma/M_AXI_S2MM capture_memory/S00_AXI
bus capture_dma/M_AXI_SG capture_memory/S01_AXI
bus capture_memory/M00_AXI ps7/S_AXI_HP0
bus adc/m_axis capture_decimate/s_axis
bus capture_decimate/m_axis capture_width/S_AXIS
bus capture_width/M_AXIS capture_fifo/S_AXIS
bus capture_fifo/M_AXIS capture_frame/s_axis
bus capture_frame/m_axis capture_cdc/S_AXIS
bus capture_cdc/M_AXIS capture_dma/S_AXIS_S2MM
net ps7/FCLK_CLK0 adc_clock/clk_in1 ps7/S_AXI_HP0_ACLK capture_memory/aclk \
    capture_dma/s_axi_lite_aclk capture_dma/m_axi_s2mm_aclk capture_dma/m_axi_sg_aclk \
    capture_cdc/m_axis_aclk capture_gpio/s_axi_aclk capture_status/clk capture_dsp/s_axi_aclk
net ps7/FCLK_RESET0_N adc_clock/resetn reset_adc/ext_reset_in
net reset_axi/peripheral_aresetn capture_memory/aresetn capture_dma/axi_resetn \
    capture_cdc/m_axis_aresetn capture_gpio/s_axi_aresetn capture_dsp/s_axi_aresetn
net adc_clock/clk_out1 adc/encode_clk
net adc_clock/clk_out2 adc/sample_clk reset_adc/slowest_sync_clk \
    capture_config/clk capture_decimate/clk capture_width/aclk capture_fifo/s_axis_aclk capture_frame/clk capture_cdc/s_axis_aclk capture_dsp/sample_clk
net adc_clock/locked reset_adc/dcm_locked
net reset_adc/peripheral_aresetn adc/resetn capture_dsp/sample_resetn
net capture_gpio/gpio_io_o capture_config/control
net capture_config/enable adc/enable
net capture_config/clock_shift adc/clock_shift
net capture_decimate/peak_detect capture_frame/peak_detect
net capture_config/group_divider capture_decimate/group_divider
net capture_config/sample_rate capture_frame/sample_rate
net capture_config/payload_bytes capture_frame/payload_bytes
net capture_config/probe_control capture_frame/probe_control
net capture_dsp/dsp_settings capture_frame/dsp_configuration
net adc/stream_resetn capture_decimate/resetn capture_width/aresetn capture_fifo/s_axis_aresetn capture_frame/resetn capture_cdc/s_axis_aresetn
net adc/overflow capture_status/overflow
net capture_status/status capture_gpio/gpio2_io_i
net capture_dma/s2mm_introut interrupts/In1
foreach port {adc0_a adc0_b adc1_a adc1_b} {
    connect_bd_net [create_bd_port -dir I -from 7 -to 0 $port] [get_bd_pins adc/$port]
}
foreach port {adc_clk_a adc_clk_b} {
    connect_bd_net [create_bd_port -dir O $port] [get_bd_pins adc/$port]
}
