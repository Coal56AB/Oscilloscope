# XS1 uses paired odd/even numbering; CAM1 in the MINI PDF uses two
# consecutive rows: XS1 even 2*k = CAM1 k; XS1 odd 2*k-1 = CAM1 41-k.
# Cross-check power: XS1 1/2=5V/GND, 37/38=GND, 39/40=3V3.
# Logical adc*_a/adc_clk_a connect to physical B, adc*_b/adc_clk_b to A.
# ADC0 D3A: XS1.15 -> CAM1.33 -> N17; D5A: XS1.17 -> CAM1.32 -> W14.
set_property PACKAGE_PIN Y19 [get_ports {adc0_a[0]}]
set_property PACKAGE_PIN W18 [get_ports {adc0_a[1]}]
set_property PACKAGE_PIN Y18 [get_ports {adc0_a[2]}]
set_property PACKAGE_PIN V18 [get_ports {adc0_a[3]}]
set_property PACKAGE_PIN V17 [get_ports {adc0_a[4]}]
set_property PACKAGE_PIN U17 [get_ports {adc0_a[5]}]
set_property PACKAGE_PIN V15 [get_ports {adc0_a[6]}]
set_property PACKAGE_PIN W15 [get_ports {adc0_a[7]}]
set_property PACKAGE_PIN U19 [get_ports {adc0_b[0]}]
set_property PACKAGE_PIN W19 [get_ports {adc0_b[1]}]
set_property PACKAGE_PIN U14 [get_ports {adc0_b[2]}]
set_property PACKAGE_PIN N17 [get_ports {adc0_b[3]}]
set_property PACKAGE_PIN Y14 [get_ports {adc0_b[4]}]
set_property PACKAGE_PIN W14 [get_ports {adc0_b[5]}]
set_property PACKAGE_PIN V16 [get_ports {adc0_b[6]}]
set_property PACKAGE_PIN P16 [get_ports {adc0_b[7]}]
set_property PACKAGE_PIN R18 [get_ports {adc1_a[0]}]
set_property PACKAGE_PIN W20 [get_ports {adc1_a[1]}]
set_property PACKAGE_PIN W16 [get_ports {adc1_a[2]}]
set_property PACKAGE_PIN R17 [get_ports {adc1_a[3]}]
set_property PACKAGE_PIN T16 [get_ports {adc1_a[4]}]
set_property PACKAGE_PIN T17 [get_ports {adc1_a[5]}]
set_property PACKAGE_PIN R16 [get_ports {adc1_a[6]}]
set_property PACKAGE_PIN U18 [get_ports {adc1_a[7]}]
set_property PACKAGE_PIN V20 [get_ports {adc1_b[0]}]
set_property PACKAGE_PIN P19 [get_ports {adc1_b[1]}]
set_property PACKAGE_PIN U20 [get_ports {adc1_b[2]}]
set_property PACKAGE_PIN P18 [get_ports {adc1_b[3]}]
set_property PACKAGE_PIN T20 [get_ports {adc1_b[4]}]
set_property PACKAGE_PIN N18 [get_ports {adc1_b[5]}]
set_property PACKAGE_PIN P20 [get_ports {adc1_b[6]}]
set_property PACKAGE_PIN N20 [get_ports {adc1_b[7]}]
set_property PACKAGE_PIN P15 [get_ports adc_clk_a]
set_property PACKAGE_PIN U15 [get_ports adc_clk_b]
set_property IOSTANDARD LVCMOS33 [get_ports {adc0_* adc1_* adc_clk_*}]
set_property DRIVE 4 [get_ports adc_clk_*]
set_property SLEW FAST [get_ports adc_clk_*]
# Return path budget: AD9288 tV(min)=2 ns, tPD(max)=6 ns plus up to
# 1.5 ns PCB round trip. Board skew has not been measured.
create_generated_clock -name adc_encode_a -source [get_pins -hier -filter {NAME =~ */clock_a/C}] -divide_by 1 [get_ports adc_clk_a]
create_generated_clock -name adc_encode_b -source [get_pins -hier -filter {NAME =~ */clock_b/C}] -divide_by 1 -invert [get_ports adc_clk_b]
set_input_delay -clock adc_encode_a -min 2.0 [get_ports {adc0_a[*] adc1_a[*]}]
set_input_delay -clock adc_encode_a -max 7.5 [get_ports {adc0_a[*] adc1_a[*]}]
set_input_delay -clock adc_encode_b -min 2.0 [get_ports {adc0_b[*] adc1_b[*]}]
set_input_delay -clock adc_encode_b -max 7.5 [get_ports {adc0_b[*] adc1_b[*]}]
# Receive the preceding encode word: setup spans 10 ns + receive phase.
# Keep the default hold relationship to the NEXT encode edge (no -hold 1):
# moving hold back one cycle would hide overwriting the word being captured.
set_multicycle_path 2 -setup -from [get_ports {adc0_a[*] adc0_b[*] adc1_a[*] adc1_b[*]}]
