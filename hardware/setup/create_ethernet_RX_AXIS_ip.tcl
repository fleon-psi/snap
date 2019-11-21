set root_dir        $::env(SNAP_HARDWARE_ROOT)
set fpga_part       $::env(FPGACHIP)
set ip_dir          $root_dir/ip
#set action_root     $::env(ACTION_ROOT)

set project_name "ethernet_ip_rx"
set project_dir [file dirname [file dirname [file normalize [info script]]]]
source $root_dir/setup/util.tcl

create_project $project_name $ip_dir/$project_name -part $fpga_part

create_bd_design $project_name
set_property  ip_repo_paths [concat [get_property ip_repo_paths [current_project]] $ip_dir] [current_project]
update_ip_catalog -rebuild -scan_changes 

addip cmac_usplus cmac_usplus_0
addip xlconstant zero
addip xlconstant one
addip xlconstant zeroX10
addip xlconstant zeroX12
addip xlconstant zeroX16
addip xlconstant zeroX56

#create_bd_cell -type ip -vlnv xilinx.com:ip:axis_clock_converter:1.1 axis_clock_converter_0
#create_bd_cell -type ip -vlnv xilinx.com:ip:axis_clock_converter:1.1 axis_clock_converter_0
addip axis_clock_converter axis_clock_converter_rx
#addip axis_clock_converter axis_clock_converter_tx

#external ports
create_bd_intf_port -mode Master -vlnv xilinx.com:interface:axis_rtl:1.0 o_rx_axis
#create_bd_intf_port -mode Slave -vlnv xilinx.com:interface:axis_rtl:1.0 i_tx_axis

#set_property CONFIG.TDATA_NUM_BYTES 64 [get_bd_intf_ports i_tx_axis]
#set_property CONFIG.HAS_TLAST 1 [get_bd_intf_ports i_tx_axis]
#set_property CONFIG.HAS_TKEEP 1 [get_bd_intf_ports i_tx_axis]
#set_property CONFIG.FREQ_HZ 250000000 [get_bd_intf_ports i_tx_axis]

create_bd_intf_port -mode Slave -vlnv xilinx.com:display_cmac_usplus:gt_ports:2.0 i_gt_rx 
#create_bd_intf_port -mode Master -vlnv xilinx.com:display_cmac_usplus:gt_ports:2.0 o_gt_tx 

create_bd_intf_port -mode Slave -vlnv xilinx.com:interface:diff_clock_rtl:1.0 i_gt_ref_clk
create_bd_port -dir I -type clk -freq_hz 250000000 i_capi_clk
set_property CONFIG.FREQ_HZ 161132812 [get_bd_intf_ports i_gt_ref_clk]
create_bd_port -dir I -type rst i_rst


## According to Alpha Data clock frequency is 161.1328125
## Vivado 2019 likes NUM_LANES 4x25
set_property -dict [list CONFIG.CMAC_CAUI4_MODE {1} CONFIG.NUM_LANES {4x25} CONFIG.GT_REF_CLK_FREQ {161.1328125} CONFIG.GT_DRP_CLK {250.0} CONFIG.RX_CHECK_PREAMBLE {1} CONFIG.RX_CHECK_SFD {1} CONFIG.TX_FLOW_CONTROL {0} CONFIG.RX_FLOW_CONTROL {0} CONFIG.ENABLE_AXI_INTERFACE {0} CONFIG.CMAC_CORE_SELECT {CMACE4_X0Y1} CONFIG.GT_GROUP_SELECT {X0Y12~X0Y15} CONFIG.LANE1_GT_LOC {X0Y12} CONFIG.LANE2_GT_LOC {X0Y13} CONFIG.LANE3_GT_LOC {X0Y14} CONFIG.LANE4_GT_LOC {X0Y15} CONFIG.LANE5_GT_LOC {NA} CONFIG.LANE6_GT_LOC {NA} CONFIG.LANE7_GT_LOC {NA} CONFIG.LANE8_GT_LOC {NA} CONFIG.LANE9_GT_LOC {NA} CONFIG.LANE10_GT_LOC {NA}] [get_bd_cells cmac_usplus_0]
set_property -dict [list CONFIG.USER_INTERFACE {AXIS}] [get_bd_cells cmac_usplus_0]

## RS_FEC necessary for Mellanox switches
set_property -dict [list CONFIG.INCLUDE_RS_FEC {1}] [get_bd_cells cmac_usplus_0]

set_property -dict [list CONFIG.OPERATING_MODE {Simplex RX}] [get_bd_cells cmac_usplus_0]

set_property -dict [list CONFIG.CONST_VAL {0}] [get_bd_cells zero]
set_property -dict [list CONFIG.CONST_WIDTH {16} CONFIG.CONST_VAL {0}] [get_bd_cells zeroX16]
set_property -dict [list CONFIG.CONST_WIDTH {12} CONFIG.CONST_VAL {0}] [get_bd_cells zeroX12]
set_property -dict [list CONFIG.CONST_WIDTH {10} CONFIG.CONST_VAL {0}] [get_bd_cells zeroX10]
#set_property -dict [list CONFIG.CONST_WIDTH {56} CONFIG.CONST_VAL {0}] [get_bd_cells zeroX56]

connect_bd_intf_net [get_bd_intf_ports i_gt_rx] [get_bd_intf_pins cmac_usplus_0/gt_rx]
#connect_bd_intf_net [get_bd_intf_ports o_gt_tx] [get_bd_intf_pins cmac_usplus_0/gt_tx]

connect_bd_net [get_bd_pins cmac_usplus_0/gt_txusrclk2] [get_bd_pins cmac_usplus_0/rx_clk]

connect_bd_net [get_bd_pins zero/dout] [get_bd_pins cmac_usplus_0/core_drp_reset]
connect_bd_net [get_bd_pins zero/dout] [get_bd_pins cmac_usplus_0/drp_we]
connect_bd_net [get_bd_pins zero/dout] [get_bd_pins cmac_usplus_0/drp_en]
connect_bd_net [get_bd_pins zero/dout] [get_bd_pins cmac_usplus_0/gtwiz_reset_tx_datapath]
connect_bd_net [get_bd_pins zero/dout] [get_bd_pins cmac_usplus_0/gtwiz_reset_rx_datapath]
connect_bd_net [get_bd_pins zero/dout] [get_bd_pins cmac_usplus_0/ctl_rsfec_ieee_error_indication_mode]
connect_bd_net [get_bd_pins zeroX10/dout] [get_bd_pins cmac_usplus_0/drp_addr]
connect_bd_net [get_bd_pins zeroX12/dout] [get_bd_pins cmac_usplus_0/gt_loopback_in]
connect_bd_net [get_bd_pins zeroX16/dout] [get_bd_pins cmac_usplus_0/drp_di]

connect_bd_net [get_bd_pins one/dout] [get_bd_pins cmac_usplus_0/ctl_rx_enable]
connect_bd_net [get_bd_pins zero/dout] [get_bd_pins cmac_usplus_0/ctl_rx_force_resync]
connect_bd_net [get_bd_pins zero/dout] [get_bd_pins cmac_usplus_0/ctl_rx_test_pattern]

connect_bd_net [get_bd_pins one/dout] [get_bd_pins cmac_usplus_0/ctl_rx_rsfec_enable]
connect_bd_net [get_bd_pins zero/dout] [get_bd_pins cmac_usplus_0/ctl_rx_rsfec_enable_correction]
connect_bd_net [get_bd_pins zero/dout] [get_bd_pins cmac_usplus_0/ctl_rx_rsfec_enable_indication]

#connect_bd_net [get_bd_pins one/dout] [get_bd_pins cmac_usplus_0/ctl_tx_enable]
#connect_bd_net [get_bd_pins zero/dout] [get_bd_pins cmac_usplus_0/ctl_tx_test_pattern]
#connect_bd_net [get_bd_pins zero/dout] [get_bd_pins cmac_usplus_0/ctl_tx_send_idle]
#connect_bd_net [get_bd_pins zero/dout] [get_bd_pins cmac_usplus_0/ctl_tx_send_rfi]
#connect_bd_net [get_bd_pins zero/dout] [get_bd_pins cmac_usplus_0/ctl_tx_send_lfi]
#connect_bd_net [get_bd_pins zeroX56/dout] [get_bd_pins cmac_usplus_0/tx_preamblein]

#connect_bd_net [get_bd_pins one/dout] [get_bd_pins cmac_usplus_0/ctl_tx_rsfec_enable]

connect_bd_net [get_bd_pins i_capi_clk] [get_bd_pins cmac_usplus_0/drp_clk]
connect_bd_net [get_bd_pins i_capi_clk] [get_bd_pins cmac_usplus_0/init_clk]
connect_bd_intf_net [get_bd_intf_ports i_gt_ref_clk] [get_bd_intf_pins cmac_usplus_0/gt_ref_clk]

set_property CONFIG.POLARITY ACTIVE_HIGH [get_bd_ports i_rst]
connect_bd_net [get_bd_pins i_rst] [get_bd_pins cmac_usplus_0/core_rx_reset]
#connect_bd_net [get_bd_pins i_rst] [get_bd_pins cmac_usplus_0/core_tx_reset]
connect_bd_net [get_bd_pins i_rst] [get_bd_pins cmac_usplus_0/sys_reset]

## Testing clock converter

connect_bd_intf_net [get_bd_intf_pins cmac_usplus_0/axis_rx] [get_bd_intf_pins axis_clock_converter_rx/S_AXIS]
connect_bd_intf_net [get_bd_intf_port o_rx_axis] [get_bd_intf_pins axis_clock_converter_rx/M_AXIS]
connect_bd_net [get_bd_pins i_capi_clk] [get_bd_pins axis_clock_converter_rx/m_axis_aclk]
connect_bd_net [get_bd_pins cmac_usplus_0/gt_txusrclk2] [get_bd_pins axis_clock_converter_rx/s_axis_aclk]

#connect_bd_intf_net [get_bd_intf_pins cmac_usplus_0/axis_tx] [get_bd_intf_pins axis_clock_converter_tx/M_AXIS]
#connect_bd_intf_net [get_bd_intf_port i_tx_axis] [get_bd_intf_pins axis_clock_converter_tx/S_AXIS]
#connect_bd_net [get_bd_pins i_capi_clk] [get_bd_pins axis_clock_converter_tx/s_axis_aclk]
#connect_bd_net [get_bd_pins cmac_usplus_0/gt_txusrclk2] [get_bd_pins axis_clock_converter_tx/m_axis_aclk]

assign_bd_address
validate_bd_design
make_wrapper -files [get_files $ip_dir/$project_name/${project_name}.srcs/sources_1/bd/${project_name}/${project_name}.bd] -top
add_files -norecurse $ip_dir/$project_name/${project_name}.srcs/sources_1/bd/${project_name}/hdl/${project_name}_wrapper.v
save_bd_design
close_project
#exit
