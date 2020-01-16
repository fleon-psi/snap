#!/bin/bash

source /opt/Xilinx/Vivado/2019.2/settings64.sh
factory_bitstream=`ls -t ./hardware/build/Images/fw_*[0-9]_FACTORY.bit | head -n1`
echo "Factory bitstream=$factory_bitstream"
user_bitstream=`ls -t ./hardware/build/Images/fw_*[0-9].bit | head -n1`
echo "User bitstream=$user_bitstream"
source .snap_config.sh  # This will mainly set the flash parameters according to the selected board 
./hardware/setup/build_mcs.tcl $factory_bitstream $user_bitstream ${factory_bitstream%.bit}.mcs
