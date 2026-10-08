# xsct build.tcl /absolute/hardware /absolute/output
if {$argc != 2} { error "Expected hardware directory and software output directory" }
set here [file dirname [file normalize [info script]]]
set hardware [file normalize [lindex $argv 0]]
set output [file normalize [lindex $argv 1]]
file mkdir $output
set root [file dirname $here]
set workspace $root/oscill.sdk
sdk setws $workspace
if {![file exists $workspace/hardware]} {
    sdk createhw -name hardware -hwspec $hardware/system.hdf
} else { sdk updatehw -hw hardware -newhwspec $hardware/system.hdf }
if {![file exists $workspace/fsbl]} {
    sdk createapp -name fsbl -app {Zynq FSBL} -hwproject hardware -proc ps7_cortexa9_0
}
if {![file exists $workspace/diagnostic]} {
    sdk createapp -name diagnostic -app {Empty Application} -hwproject hardware -proc ps7_cortexa9_0
}
foreach application {fsbl diagnostic} {
    file copy -force $here/video.c $here/video.h $workspace/$application/src
    file copy -force $root/display/boot_splash.c $root/display/boot_splash.h \
        $root/display/boot_logo_data.h $workspace/$application/src
    configbsp -bsp $workspace/${application}_bsp/system.mss stdin ps7_uart_1
    configbsp -bsp $workspace/${application}_bsp/system.mss stdout ps7_uart_1
    sdk configapp -app $application compiler-optimization {Optimize more (-O2)}
}
file copy -force $here/diagnostic.c $workspace/diagnostic/src
set linker $workspace/diagnostic/src/lscript.ld
set stream [open $linker r]; set content [read $stream]; close $stream
regsub {LENGTH = 0x1FF00000} $content {LENGTH = 0x01F00000} content
set stream [open $linker w]; puts $stream $content; close $stream
if {[lsearch -exact [sdk configapp -app fsbl define-compiler-symbols] FSBL_DEBUG_INFO] < 0} {
    sdk configapp -app fsbl define-compiler-symbols FSBL_DEBUG_INFO
}
# Enable video immediately after PL configuration, before loading U-Boot.
# The handoff hook is retained for JTAG and uses the idempotent video start.
set hooks $workspace/fsbl/src/fsbl_hooks.c
set stream [open $hooks r]; set content [read $stream]; close $stream
if {[string first "#include \"video.h\"" $content] < 0} {
    set content "#include \"video.h\"\n$content"
}
if {[string first "board_video_start()" $content] < 0} {
    if {![regsub {(u32 FsblHookBeforeHandoff\(void\)[\s\n]*\{)} $content \
        "\\1\n    if (!board_video_start()) return XST_FAILURE;" content]} { error "FSBL hook missing" }
}
if {[string first "SPLASH_EARLY" $content] < 0} {
    set early {
    /* SPLASH_EARLY: the PL is configured; release PS/PL interfaces now. */
#ifdef PS7_POST_CONFIG
    if (ps7_post_config() != FSBL_PS7_INIT_SUCCESS) return XST_FAILURE;
#else
    Xil_Out32(PS_LVL_SHFTR_EN, LVL_PL_PS);
    Xil_Out32(FPGA_RESET_REG, 0);
#endif
    SlcrUnlock();
    if (!board_video_start()) return XST_FAILURE;
    fsbl_printf(DEBUG_GENERAL, "SPLASH_READY\r\n");
}
    if {![regsub {(u32 FsblHookAfterBitstreamDload\(void\)[\s\n]*\{)} $content \
        "\\1$early" content]} { error "FSBL after-bitstream hook missing" }
}
set stream [open $hooks w]; puts $stream $content; close $stream
foreach application {fsbl diagnostic} {
    file delete -force $workspace/$application/Debug/$application.elf
}
sdk projects -build -type all
foreach application {fsbl diagnostic} {
    file copy -force $workspace/$application/Debug/$application.elf $output/$application.elf
}
set stream [open $output/diagnostic.bif w]
puts $stream "image: {\n  \[bootloader\] \"$output/fsbl.elf\"\n  \"$hardware/system.bit\"\n  \"$output/diagnostic.elf\"\n}"
close $stream
puts "Run bootgen -arch zynq -image $output/diagnostic.bif -o $output/BOOT.BIN -w on"
