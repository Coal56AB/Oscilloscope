# xsct build.tcl /absolute/hardware /absolute/output
if {$argc != 2} { error "Expected hardware directory and software output directory" }
set here [file dirname [file normalize [info script]]]
set hardware [file normalize [lindex $argv 0]]
set output [file normalize [lindex $argv 1]]
file mkdir $output
sdk setws $output/sdk
if {![file exists $output/sdk/hardware]} {
    sdk createhw -name hardware -hwspec $hardware/system.hdf
} else { sdk updatehw -hw hardware -newhwspec $hardware/system.hdf }
if {![file exists $output/sdk/fsbl]} {
    sdk createapp -name fsbl -app {Zynq FSBL} -hwproject hardware -proc ps7_cortexa9_0
}
if {![file exists $output/sdk/diagnostic]} {
    sdk createapp -name diagnostic -app {Empty Application} -hwproject hardware -proc ps7_cortexa9_0
}
foreach application {fsbl diagnostic} {
    file copy -force $here/video.c $here/video.h $output/sdk/$application/src
    configbsp -bsp $output/sdk/${application}_bsp/system.mss stdin ps7_uart_1
    configbsp -bsp $output/sdk/${application}_bsp/system.mss stdout ps7_uart_1
    sdk configapp -app $application compiler-optimization {Optimize more (-O2)}
}
file copy -force $here/diagnostic.c $output/sdk/diagnostic/src
set linker $output/sdk/diagnostic/src/lscript.ld
set stream [open $linker r]; set content [read $stream]; close $stream
regsub {LENGTH = 0x1FF00000} $content {LENGTH = 0x01F00000} content
set stream [open $linker w]; puts $stream $content; close $stream
if {[lsearch -exact [sdk configapp -app fsbl define-compiler-symbols] FSBL_DEBUG_INFO] < 0} {
    sdk configapp -app fsbl define-compiler-symbols FSBL_DEBUG_INFO
}
# FSBL hook runs after the PL partition was loaded and before the next executable.
set hooks $output/sdk/fsbl/src/fsbl_hooks.c
set stream [open $hooks r]; set content [read $stream]; close $stream
if {[string first "#include \"video.h\"" $content] < 0} {
    set content "#include \"video.h\"\n$content"
}
if {[string first "board_video_start()" $content] < 0} {
    if {![regsub {(u32 FsblHookBeforeHandoff\(void\)[\s\n]*\{)} $content \
        "\\1\n    if (!board_video_start()) return XST_FAILURE;" content]} { error "FSBL hook missing" }
}
set stream [open $hooks w]; puts $stream $content; close $stream
foreach application {fsbl diagnostic} {
    file delete -force $output/sdk/$application/Debug/$application.elf
}
sdk projects -build -type all
foreach application {fsbl diagnostic} {
    file copy -force $output/sdk/$application/Debug/$application.elf $output/$application.elf
}
set stream [open $output/diagnostic.bif w]
puts $stream "image: {\n  \[bootloader\] \"$output/fsbl.elf\"\n  \"$hardware/system.bit\"\n  \"$output/diagnostic.elf\"\n}"
close $stream
puts "Run bootgen -arch zynq -image $output/diagnostic.bif -o $output/BOOT.BIN -w on"
