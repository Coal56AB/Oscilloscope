# Keep the 2019.1 date-based revision inside IP packager's signed 32-bit field.
# Only the generated project script changes; the installed tools stay intact.
proc repair_hls_revision {directory vivado} {
    set script [file join $directory run_ippack.tcl]
    set stream [open $script r];set content [read $stream];close $stream
    if {![regexp {set Revision[ \t]+"([0-9]+)"} $content unused revision] ||
        $revision <= 2147483647} { error "HLS export failed without a revision overflow" }
    set original {set_property core_revision $Revision $core}
    if {[string first $original $content] < 0} { error "Unknown HLS revision assignment" }
    set replacement {set_property core_revision [expr {$Revision & 0x7fffffff}] $core}
    set content [string map [list $original $replacement] $content]
    set stream [open $script w];puts -nonewline $stream $content;close $stream
    set previous [pwd];cd $directory
    set failed [catch {
        exec $vivado -notrace -mode batch -source run_ippack.tcl > revision-pack-console.log 2>@1
    } message options]
    cd $previous
    if {$failed} { return -options $options $message }
    if {![file exists $directory/component.xml] || ![llength [glob -nocomplain $directory/*.zip]]} {
        error "HLS IP package is incomplete"
    }
}
proc package_hls_ip {solution} {
    if {[catch {export_design -format ip_catalog -rtl verilog} message]} {
        set suffix [expr {$::tcl_platform(platform) eq "windows"?".bat":""}]
        set vivado [file normalize [file dirname [info nameofexecutable]]/../../vivado$suffix]
        repair_hls_revision [file join $solution impl ip] $vivado
    }
}
