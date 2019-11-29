set root_dir        $::env(SNAP_HARDWARE_ROOT)
set fpga_part       $::env(FPGACHIP)
set ip_dir          $root_dir/ip
#set action_root     $::env(ACTION_ROOT)

################################################################
# Check if script is running in correct Vivado version.
################################################################
set scripts_vivado_version 2019.2
set current_vivado_version [version -short]

if { [string first $scripts_vivado_version $current_vivado_version] == -1 } {
   puts ""
   catch {common::send_msg_id "BD_TCL-109" "ERROR" "This script was generated using Vivado <$scripts_vivado_version> and is being run in <$current_vivado_version> of Vivado. Please run the script in Vivado <$scripts_vivado_version> then open the design in Vivado <$current_vivado_version>. Upgrade the design by running \"Tools => Report => Report IP Status...\", then run write_bd_tcl to create an updated script."}

   return 1
}

set project_name "eth_100G"
set project_dir [file dirname [file dirname [file normalize [info script]]]]
source $root_dir/setup/util.tcl

create_project $project_name $ip_dir/$project_name -part $fpga_part

create_bd_design $project_name
set_property  ip_repo_paths [concat [get_property ip_repo_paths [current_project]] $ip_dir] [current_project]
update_ip_catalog -rebuild -scan_changes 

# Create interface ports
  set i_eth1_gt_rx [ create_bd_intf_port -mode Slave -vlnv xilinx.com:display_cmac_usplus:gt_ports:2.0 i_eth1_gt_rx ]

  set o_eth1_gt_tx [ create_bd_intf_port -mode Master -vlnv xilinx.com:display_cmac_usplus:gt_ports:2.0 o_eth1_gt_tx ]

  set i_gt_ref [ create_bd_intf_port -mode Slave -vlnv xilinx.com:interface:diff_clock_rtl:1.0 i_gt_ref ]
  set_property -dict [ list \
   CONFIG.FREQ_HZ {161132812} \
   ] $i_gt_ref

  set i_eth1_axis [ create_bd_intf_port -mode Slave -vlnv xilinx.com:interface:axis_rtl:1.0 i_eth1_axis ]
  set_property -dict [ list \
   CONFIG.FREQ_HZ {250000000} \
   ] $i_eth1_axis

  set o_eth1_axis [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:axis_rtl:1.0 o_eth1_axis ]
  set_property -dict [ list \
   CONFIG.FREQ_HZ {250000000} \
   ] $o_eth1_axis

set_property CONFIG.TDATA_NUM_BYTES 64 [get_bd_intf_ports i_eth1_axis]
set_property CONFIG.HAS_TLAST 1 [get_bd_intf_ports i_eth1_axis]
set_property CONFIG.HAS_TKEEP 1 [get_bd_intf_ports i_eth1_axis]
set_property CONFIG.FREQ_HZ 250000000 [get_bd_intf_ports i_eth1_axis]
set_property CONFIG.TUSER_WIDTH 1 [get_bd_intf_ports i_eth1_axis]

# Create ports
  set i_ctl_rx_enable [ create_bd_port -dir I i_ctl_rx_enable ]
  set i_ctl_rx_rfsec_enable [ create_bd_port -dir I i_ctl_rx_rfsec_enable ]
  set i_ctl_tx_enable [ create_bd_port -dir I i_ctl_tx_enable ]
  set i_ctl_tx_rfsec_enable [ create_bd_port -dir I i_ctl_tx_rfsec_enable ]
  set i_capi_clk [ create_bd_port -dir I -type clk -freq_hz 250000000 i_capi_clk ]
  set i_core_rx_reset [ create_bd_port -dir I -type rst i_core_rx_reset ]
  set_property -dict [ list \
   CONFIG.POLARITY {ACTIVE_HIGH} \
 ] $i_core_rx_reset
  set i_core_tx_reset [ create_bd_port -dir I -type rst i_core_tx_reset ]
  set_property -dict [ list \
   CONFIG.POLARITY {ACTIVE_HIGH} \
 ] $i_core_tx_reset
  set i_sys_rst [ create_bd_port -dir I -type rst i_sys_rst ]
  set_property -dict [ list \
   CONFIG.POLARITY {ACTIVE_HIGH} \
 ] $i_sys_rst

# Create instance: axis_clock_converter_0, and set properties
  set axis_clock_converter_rx_eth1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_clock_converter:1.1 axis_clock_converter_rx_eth1 ]

# Create instance: axis_clock_converter_1, and set properties
  set axis_clock_converter_tx_eth1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_clock_converter:1.1 axis_clock_converter_tx_eth1 ]

# Create instance: cmac_usplus_eth1, and set properties
  set cmac_usplus_eth1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:cmac_usplus:3.0 cmac_usplus_eth1 ]
  set_property -dict [ list \
   CONFIG.CMAC_CAUI4_MODE {1} \
   CONFIG.GT_DRP_CLK {250.00} \
   CONFIG.GT_GROUP_SELECT {X0Y8~X0Y11} \
   CONFIG.GT_REF_CLK_FREQ {161.1328125} \
   CONFIG.INCLUDE_RS_FEC {1} \
   CONFIG.LANE10_GT_LOC {NA} \
   CONFIG.LANE5_GT_LOC {NA} \
   CONFIG.LANE6_GT_LOC {NA} \
   CONFIG.LANE7_GT_LOC {NA} \
   CONFIG.LANE8_GT_LOC {NA} \
   CONFIG.LANE9_GT_LOC {NA} \
   CONFIG.NUM_LANES {4x25} \
   CONFIG.OPERATING_MODE {Duplex} \
   CONFIG.RX_FLOW_CONTROL {0} \
   CONFIG.TX_FLOW_CONTROL {0} \
   CONFIG.USER_INTERFACE {AXIS} \
 ] $cmac_usplus_eth1

# Create instance: xlconstant_0, and set properties
  set xlconstant_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 xlconstant_0 ]
  set_property -dict [ list \
   CONFIG.CONST_VAL {0} \
 ] $xlconstant_0

# Create interface connections
  connect_bd_intf_net -intf_net axis_clock_converter_rx_eth1_M_AXIS [get_bd_intf_ports o_eth1_axis] [get_bd_intf_pins axis_clock_converter_rx_eth1/M_AXIS]
  connect_bd_intf_net -intf_net axis_clock_converter_tx_eth1_S_AXIS [get_bd_intf_ports i_eth1_axis] [get_bd_intf_pins axis_clock_converter_tx_eth1/S_AXIS]

  connect_bd_intf_net -intf_net cmac_usplus_eth1_axis_rx [get_bd_intf_pins axis_clock_converter_rx_eth1/S_AXIS] [get_bd_intf_pins cmac_usplus_eth1/axis_rx]
  connect_bd_intf_net -intf_net cmac_usplus_eth1_axis_tx [get_bd_intf_pins axis_clock_converter_tx_eth1/M_AXIS] [get_bd_intf_pins cmac_usplus_eth1/axis_tx]

  connect_bd_intf_net -intf_net i_eth1_gt_rx_1 [get_bd_intf_ports i_eth1_gt_rx] [get_bd_intf_pins cmac_usplus_eth1/gt_rx]
  connect_bd_intf_net -intf_net o_eth1_gt_tx_1 [get_bd_intf_ports o_eth1_gt_tx] [get_bd_intf_pins cmac_usplus_eth1/gt_tx]
  connect_bd_intf_net -intf_net i_gt_ref_1 [get_bd_intf_ports i_gt_ref] [get_bd_intf_pins cmac_usplus_eth1/gt_ref_clk]

# Create port connections
  connect_bd_net -net cmac_usplus_eth1_gt_txusrclk2 [get_bd_pins cmac_usplus_eth1/gt_txusrclk2] [get_bd_pins axis_clock_converter_tx_eth1/m_axis_aclk]
  connect_bd_net -net cmac_usplus_eth1_gt_rxusrclk2 [get_bd_pins axis_clock_converter_rx_eth1/s_axis_aclk] [get_bd_pins cmac_usplus_eth1/gt_rxusrclk2] [get_bd_pins cmac_usplus_eth1/rx_clk]
  connect_bd_net -net i_ctl_rx_enable_1 [get_bd_ports i_ctl_rx_enable] [get_bd_pins cmac_usplus_eth1/ctl_rx_enable]
  connect_bd_net -net i_ctl_rx_rfsec_enable_1 [get_bd_ports i_ctl_rx_rfsec_enable] [get_bd_pins cmac_usplus_eth1/ctl_rx_rsfec_enable]
  connect_bd_net -net i_ctl_tx_enable_1 [get_bd_ports i_ctl_tx_enable] [get_bd_pins cmac_usplus_eth1/ctl_tx_enable]
  connect_bd_net -net i_ctl_tx_rfsec_enable_1 [get_bd_ports i_ctl_tx_rfsec_enable] [get_bd_pins cmac_usplus_eth1/ctl_tx_rsfec_enable]
  connect_bd_net -net i_capi_clk_1 [get_bd_ports i_capi_clk] [get_bd_pins axis_clock_converter_rx_eth1/m_axis_aclk] [get_bd_pins axis_clock_converter_tx_eth1/s_axis_aclk] [get_bd_pins cmac_usplus_eth1/drp_clk] [get_bd_pins cmac_usplus_eth1/init_clk]
  connect_bd_net -net i_core_rx_reset_1 [get_bd_ports i_core_rx_reset] [get_bd_pins cmac_usplus_eth1/core_rx_reset]
  connect_bd_net -net i_core_tx_reset_1 [get_bd_ports i_core_tx_reset] [get_bd_pins cmac_usplus_eth1/core_tx_reset]
  connect_bd_net -net i_rst_1 [get_bd_ports i_sys_rst] [get_bd_pins cmac_usplus_eth1/sys_reset]
  connect_bd_net -net xlconstant_0_dout [get_bd_pins cmac_usplus_eth1/core_drp_reset] [get_bd_pins cmac_usplus_eth1/ctl_rsfec_ieee_error_indication_mode] [get_bd_pins cmac_usplus_eth1/ctl_rx_force_resync] [get_bd_pins cmac_usplus_eth1/ctl_rx_rsfec_enable_correction] [get_bd_pins cmac_usplus_eth1/ctl_rx_rsfec_enable_indication] [get_bd_pins cmac_usplus_eth1/ctl_rx_test_pattern] [get_bd_pins cmac_usplus_eth1/drp_en] [get_bd_pins cmac_usplus_eth1/drp_we] [get_bd_pins cmac_usplus_eth1/gtwiz_reset_rx_datapath] [get_bd_pins cmac_usplus_eth1/gtwiz_reset_tx_datapath] [get_bd_pins xlconstant_0/dout]

assign_bd_address
validate_bd_design
make_wrapper -files [get_files $ip_dir/$project_name/${project_name}.srcs/sources_1/bd/${project_name}/${project_name}.bd] -top
add_files -norecurse $ip_dir/$project_name/${project_name}.srcs/sources_1/bd/${project_name}/hdl/${project_name}_wrapper.v
save_bd_design
close_project
#exit
