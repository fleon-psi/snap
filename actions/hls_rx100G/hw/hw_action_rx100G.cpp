/*
 * Copyright 2017 International Business Machines
 * Copyright 2019 Paul Scherrer Institute
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>
#include "ap_int.h"
#include "hw_action_rx100G.h"

inline void mask_tkeep(ap_uint<512> &data, ap_uint<64> keep) {
#pragma HLS UNROLL
	for (int i = 0; i < 64; i++) {
		if (keep[i] == 0) data(i*8+7,i*8) = 0;
	}
}

void prepare_hbm(snap_membus_t *din_gmem, size_t in_gain_pedestal_addr,
		snap_membus_t *d_hbm_p0,  snap_membus_t *d_hbm_p1,
		snap_membus_t *d_hbm_p2,  snap_membus_t *d_hbm_p3,
		snap_membus_t *d_hbm_p4,  snap_membus_t *d_hbm_p5,
		snap_membus_t *d_hbm_p6,  snap_membus_t *d_hbm_p7,
		snap_membus_t *d_hbm_p8,  snap_membus_t *d_hbm_p9,
		snap_membus_t *d_hbm_p10, snap_membus_t *d_hbm_p11) {
	ap_uint<512> zero512 = 0;

	for (int i = 0; i < NMODULES; i ++) {
	}
}

void process_frames(AXI_STREAM &din_eth,
		eth_settings_t eth_settings, eth_stat_t &eth_stat, writer_settings_t writer_settings,
		snap_membus_t *dout_gmem, size_t in_gain_pedestal_addr,
		size_t out_frame_buffer_addr, size_t out_frame_status_addr,
		snap_membus_t *d_hbm_p0,  snap_membus_t *d_hbm_p1,
		snap_membus_t *d_hbm_p2,  snap_membus_t *d_hbm_p3,
		snap_membus_t *d_hbm_p4,  snap_membus_t *d_hbm_p5,
		snap_membus_t *d_hbm_p6,  snap_membus_t *d_hbm_p7,
		snap_membus_t *d_hbm_p8,  snap_membus_t *d_hbm_p9,
		snap_membus_t *d_hbm_p10, snap_membus_t *d_hbm_p11) {
#pragma HLS DATAFLOW
	DATA_STREAM raw;
#pragma HLS STREAM variable=raw depth=2048
	read_eth_packet(din_eth, raw, eth_settings, eth_stat);
	write_data(raw, dout_gmem, in_gain_pedestal_addr, out_frame_buffer_addr, out_frame_status_addr, writer_settings,
			d_hbm_p0, d_hbm_p1, d_hbm_p2, d_hbm_p3, d_hbm_p4, d_hbm_p5, d_hbm_p6, d_hbm_p7, d_hbm_p8, d_hbm_p9, d_hbm_p10, d_hbm_p11);
}

//----------------------------------------------------------------------
//--- MAIN PROGRAM -----------------------------------------------------
//----------------------------------------------------------------------
static int process_action(snap_membus_t *din_gmem,
		snap_membus_t *dout_gmem,
		snap_membus_t *d_hbm_p0,  snap_membus_t *d_hbm_p1,
		snap_membus_t *d_hbm_p2,  snap_membus_t *d_hbm_p3,
		snap_membus_t *d_hbm_p4,  snap_membus_t *d_hbm_p5,
		snap_membus_t *d_hbm_p6,  snap_membus_t *d_hbm_p7,
		snap_membus_t *d_hbm_p8,  snap_membus_t *d_hbm_p9,
		snap_membus_t *d_hbm_p10, snap_membus_t *d_hbm_p11,
		AXI_STREAM &din_eth, AXI_STREAM &dout_eth,
		action_reg *act_reg)
{

	send_gratious_arp(dout_eth, act_reg->Data.fpga_mac_addr, act_reg->Data.fpga_ipv4_addr);

	size_t in_gain_pedestal_addr = act_reg->Data.in_gain_pedestal_data.addr >> ADDR_RIGHT_SHIFT;
	size_t out_frame_buffer_addr = act_reg->Data.out_frame_buffer.addr >> ADDR_RIGHT_SHIFT;
	size_t out_frame_status_addr = act_reg->Data.out_frame_status.addr >> ADDR_RIGHT_SHIFT;

	uint64_t bytes_written = 0;

	eth_settings_t eth_settings;
	eth_settings.fpga_mac_addr = act_reg->Data.fpga_mac_addr;
	eth_settings.fpga_ipv4_addr = act_reg->Data.fpga_ipv4_addr;
	eth_settings.expected_packets = act_reg->Data.packets_to_read;

	eth_stat_t eth_stats;
	eth_stats.bad_packets = 0;
	eth_stats.good_packets = 0;
	eth_stats.ignored_packets = 0;

	writer_settings_t writer_settings;
	// Stupid rule for the moment.
	if (act_reg->Data.pedestalG0_frames > 0) writer_settings.convert = true;
	else writer_settings.convert = false;
	writer_settings.pedestalG0 = act_reg->Data.pedestalG0_frames;
	writer_settings.pedestalG1 = act_reg->Data.pedestalG1_frames;
	writer_settings.pedestalG2 = act_reg->Data.pedestalG2_frames;
	writer_settings.total_frames = act_reg->Data.packets_to_read / 128;

	process_frames(din_eth, eth_settings, eth_stats, writer_settings, dout_gmem, in_gain_pedestal_addr, out_frame_buffer_addr, out_frame_status_addr,
			d_hbm_p0, d_hbm_p1, d_hbm_p2, d_hbm_p3, d_hbm_p4, d_hbm_p5, d_hbm_p6, d_hbm_p7, d_hbm_p8, d_hbm_p9, d_hbm_p10, d_hbm_p11);

	act_reg->Data.good_packets = eth_stats.good_packets;
	act_reg->Data.bad_packets = eth_stats.bad_packets;
	act_reg->Data.ignored_packets = eth_stats.ignored_packets;

	act_reg->Control.Retc = SNAP_RETC_SUCCESS;

	return 0;
}

//--- TOP LEVEL MODULE -------------------------------------------------
void hls_action(snap_membus_t *din_gmem,
		snap_membus_t *dout_gmem,
		snap_membus_t *d_hbm_p0,  snap_membus_t *d_hbm_p1,
		snap_membus_t *d_hbm_p2,  snap_membus_t *d_hbm_p3,
		snap_membus_t *d_hbm_p4,  snap_membus_t *d_hbm_p5,
		snap_membus_t *d_hbm_p6,  snap_membus_t *d_hbm_p7,
		snap_membus_t *d_hbm_p8,  snap_membus_t *d_hbm_p9,
		snap_membus_t *d_hbm_p10, snap_membus_t *d_hbm_p11,
		AXI_STREAM &din_eth,
		AXI_STREAM &dout_eth,
		action_reg *act_reg,
		action_RO_config_reg *Action_Config)
{
	// Host Memory AXI Interface - CANNOT BE REMOVED - NO CHANGE BELOW
#pragma HLS INTERFACE m_axi port=din_gmem bundle=host_mem offset=slave depth=512 \
		max_read_burst_length=64  max_write_burst_length=64 latency=16
#pragma HLS INTERFACE s_axilite port=din_gmem bundle=ctrl_reg offset=0x030

#pragma HLS INTERFACE m_axi port=dout_gmem bundle=host_mem offset=slave depth=512 \
		max_read_burst_length=64  max_write_burst_length=64 latency=16
#pragma HLS INTERFACE s_axilite port=dout_gmem bundle=ctrl_reg offset=0x040

	/*  // DDR memory Interface - CAN BE COMMENTED IF UNUSED
	 * #pragma HLS INTERFACE m_axi port=d_ddrmem bundle=card_mem0 offset=slave depth=512 \
	 *   max_read_burst_length=64  max_write_burst_length=64
	 * #pragma HLS INTERFACE s_axilite port=d_ddrmem bundle=ctrl_reg offset=0x050
	 */
	// Host Memory AXI Lite Master Interface - NO CHANGE BELOW
#pragma HLS DATA_PACK variable=Action_Config
#pragma HLS INTERFACE s_axilite port=Action_Config bundle=ctrl_reg offset=0x010
#pragma HLS DATA_PACK variable=act_reg
#pragma HLS INTERFACE s_axilite port=act_reg bundle=ctrl_reg offset=0x100
#pragma HLS INTERFACE s_axilite port=return bundle=ctrl_reg

#pragma HLS INTERFACE m_axi port=d_hbm_p0 bundle=card_hbm_p0 offset=slave depth=512 \
		max_read_burst_length=64  max_write_burst_length=64 latency=16
#pragma HLS INTERFACE m_axi port=d_hbm_p1 bundle=card_hbm_p1 offset=slave depth=512 \
		max_read_burst_length=64  max_write_burst_length=64 latency=16
#pragma HLS INTERFACE m_axi port=d_hbm_p2 bundle=card_hbm_p2 offset=slave depth=512 \
		max_read_burst_length=64  max_write_burst_length=64 latency=16
#pragma HLS INTERFACE m_axi port=d_hbm_p3 bundle=card_hbm_p3 offset=slave depth=512 \
		max_read_burst_length=64  max_write_burst_length=64 latency=16
#pragma HLS INTERFACE m_axi port=d_hbm_p4 bundle=card_hbm_p4 offset=slave depth=512 \
		max_read_burst_length=64  max_write_burst_length=64 latency=16
#pragma HLS INTERFACE m_axi port=d_hbm_p5 bundle=card_hbm_p5 offset=slave depth=512 \
		max_read_burst_length=64  max_write_burst_length=64 latency=16
#pragma HLS INTERFACE m_axi port=d_hbm_p6 bundle=card_hbm_p6 offset=slave depth=512 \
		max_read_burst_length=64  max_write_burst_length=64 latency=16
#pragma HLS INTERFACE m_axi port=d_hbm_p7 bundle=card_hbm_p7 offset=slave depth=512 \
		max_read_burst_length=64  max_write_burst_length=64 latency=16
#pragma HLS INTERFACE m_axi port=d_hbm_p8 bundle=card_hbm_p8 offset=slave depth=512 \
		max_read_burst_length=64  max_write_burst_length=64 latency=16
#pragma HLS INTERFACE m_axi port=d_hbm_p9 bundle=card_hbm_p9 offset=slave depth=512 \
		max_read_burst_length=64  max_write_burst_length=64 latency=16
#pragma HLS INTERFACE m_axi port=d_hbm_p10 bundle=card_hbm_p10 offset=slave depth=512 \
		max_read_burst_length=64  max_write_burst_length=64 latency=16
#pragma HLS INTERFACE m_axi port=d_hbm_p11 bundle=card_hbm_p11 offset=slave depth=512 \
		max_read_burst_length=64  max_write_burst_length=64 latency=16


#pragma HLS INTERFACE axis register off port=din_eth
#pragma HLS INTERFACE axis register off port=dout_eth


	/* Required Action Type Detection - NO CHANGE BELOW */
	//	NOTE: switch generates better vhdl than "if" */
	// Test used to exit the action if no parameter has been set.
	// Used for the discovery phase of the cards */
	switch (act_reg->Control.flags) {
	case 0:
		Action_Config->action_type = RX100G_ACTION_TYPE; //TO BE ADAPTED
		Action_Config->release_level = RELEASE_LEVEL;
		act_reg->Control.Retc = 0xe00f;
		return;
		break;
	default:
		/* process_action(din_gmem, dout_gmem, d_ddrmem, act_reg); */
		// process_action(din_gmem, dout_gmem, din_eth, dout_eth, act_reg);
		process_action(din_gmem, dout_gmem,
				d_hbm_p0, d_hbm_p1, d_hbm_p2, d_hbm_p3, d_hbm_p4, d_hbm_p5, d_hbm_p6, d_hbm_p7, d_hbm_p8, d_hbm_p9, d_hbm_p10, d_hbm_p11,
				din_eth, dout_eth, act_reg);
		break;
	}
}

