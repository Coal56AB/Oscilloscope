# Run against a clean checkout to verify the saved project without old output.
if {$argc != 1} { error "Expected an oscill.xpr path" }
set project [file normalize [lindex $argv 0]]
set root [file dirname $project]
open_project $project
foreach repository [get_property IP_REPO_PATHS [current_project]] {
    if {[string first "$root/" [file normalize $repository]] != 0} {
        error "IP repository outside the project: $repository"
    }
}
set bd [get_files system.bd]
open_bd_design $bd
validate_bd_design
foreach ip [get_ips -all] {
    if {[get_property IS_LOCKED $ip]} { error "Locked IP: $ip" }
}
generate_target all $bd
update_compile_order -fileset sources_1
foreach source [get_files -compile_order sources -used_in synthesis] {
    if {![file exists $source]} { error "Missing source: $source" }
}
foreach report [get_report_configs] {
    if {[get_property IS_ENABLED $report]} { error "Automatic report is enabled: $report" }
}
close_project
puts "OSCILL PROJECT PASS"
