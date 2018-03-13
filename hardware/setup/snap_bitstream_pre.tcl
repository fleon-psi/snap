#-----------------------------------------------------------
#
# Copyright 2016, International Business Machines
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
#-----------------------------------------------------------

set factory_image [string toupper $::env(FACTORY_IMAGE)]

set_property BITSTREAM.GENERAL.COMPRESS TRUE [current_design]
set_property BITSTREAM.CONFIG.EXTMASTERCCLK_EN {DIV-4} [current_design]
set_property CONFIG_MODE BPI16 [current_design]
set_property BITSTREAM.CONFIG.BPI_SYNC_MODE DISABLE [current_design]		# default disable
set_property BITSTREAM.CONFIG.BPI_1ST_READ_CYCLE 4 [current_design]
set_property BITSTREAM.CONFIG.BPI_PAGE_SIZE 8 [current_design]

set_property BITSTREAM.CONFIG.UNUSEDPIN Pullnone [current_design]		# default pulldown, doesn't load at power-on!
set_property BITSTREAM.CONFIG.OVERTEMPSHUTDOWN Enable [current_design]		# default disable
set_property CONFIG_VOLTAGE 1.8 [current_design]
set_property BITSTREAM.CONFIG.PERSIST NO [current_design] 			# default NO anyhow

# xapp1246/xapp1296/ug908: These settings may not be needed for SNAP
# set_property BITSTREAM.CONFIG.CONFIGFALLBACK ENABLE [current_design]		# default enable
set_property BITSTREAM.CONFIG.TIMER_CFG 0XFFFFFFFF [current_design]		# no watchdog

# The factory bitstream has the above properties plus:
if { $factory_image == "TRUE" } {
  #xapp1246/xapp1296: These settings are not needed for SNAP. 
  #FIXME remove when testing was successful
  # set_property BITSTREAM.CONFIG.NEXT_CONFIG_ADDR 0X01000000 [current_design]	# default is 0x0
  set_property BITSTREAM.CONFIG.REVISIONSELECT_TRISTATE ENABLE [current_design] # default enable
  set_property BITSTREAM.CONFIG.REVISIONSELECT 01 [current_design] 		# default is 00
}
