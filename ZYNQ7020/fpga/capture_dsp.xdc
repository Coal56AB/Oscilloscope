# Staging is frozen before the request toggle crosses two synchronizers and
# stays frozen until acknowledgement returns. Bound the bundled data path
# to one sample-clock period, inside that two-clock settling interval.
set transfer_cells [get_cells -hier -filter {NAME =~ *capture_dsp*transfer_reg*}]
set configuration_cells [get_cells -hier -filter {NAME =~ *capture_dsp*dsp_settings_reg*}]
set_max_delay -datapath_only 10.0 -from $transfer_cells -to $configuration_cells
set first_sync [get_pins -hier -filter {NAME =~ *capture_dsp*request_meta_reg/D || NAME =~ *capture_dsp*ack_meta_reg/D}]
set_false_path -to $first_sync
