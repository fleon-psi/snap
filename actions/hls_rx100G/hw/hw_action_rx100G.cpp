/*
 * Copyright 2017 International Business Machines
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

enum rcv_state_t {RCV_INIT, RCV_GOOD, RCV_BAD, RCV_IGNORE};

inline void mask_tkeep(ap_uint<512> &data, ap_uint<64> keep) {
#pragma HLS UNROLL
	for (int i = 0; i < 64; i++) {
		if (keep[i] == 0) data(i*8+7,i*8) = 0;
	}
}
//----------------------------------------------------------------------
//--- MAIN PROGRAM -----------------------------------------------------
//----------------------------------------------------------------------
static int process_action(snap_membus_t *din_gmem,
		snap_membus_t *dout_gmem,
		AXI_STREAM &din_eth,
		action_reg *act_reg)
{
	uint64_t o_idx = 0;
	uint64_t out_offset = act_reg->Data.out.addr >> ADDR_RIGHT_SHIFT;
	size_t bytes_read = 0;

	ap_axiu_for_eth packet_in;

	word_t text;

	size_t packets_read = 0;
	rcv_state_t rcv_state = RCV_INIT;
	packet_header_t packet_header;

	act_reg->Data.good_packets = 0;
	act_reg->Data.ignored_packets = 0;
	act_reg->Data.bad_packets = 0;
	act_reg->Data.user = 0;

	while (packets_read < act_reg->Data.packets_to_read) {
#pragma HLS PIPELINE
		din_eth.read(packet_in);
		//if (packet_in.last == 1) rcv_state = RCV_IGNORE;
		switch (rcv_state) {
		case RCV_INIT:
			decode_eth_1(packet_in.data, packet_header);
			if ((packet_header.dest_mac == act_reg->Data.fpga_mac_addr) &&
					(packet_header.ether_type == 0x0800) && // IP
					(packet_header.ip_version == 4) && // IPv4
					(packet_header.ipv4_protocol == 0x11) && // UDP
					(packet_header.ipv4_total_len == 8268) && // Packet length is consistent with JUNGFRAU
					(packet_in.last == 0))
			{
				rcv_state = RCV_GOOD;
				memcpy(dout_gmem + out_offset + o_idx, (char *) (&packet_in.data), BPERDW);
				bytes_read += 64;
				o_idx++;
			} else {
				rcv_state = RCV_IGNORE;
				act_reg->Data.ignored_packets++;
			}
			break;

		case RCV_GOOD:
			if (o_idx >= 130) rcv_state = RCV_BAD; // Cannot receive more than 130
			else {
				ap_uint<512> tmp = packet_in.data;
				mask_tkeep(tmp,packet_in.keep);
				memcpy(dout_gmem + out_offset + o_idx, (char *) (&tmp), BPERDW);
				o_idx++;
				bytes_read += 64;
			}
			break;
		case RCV_IGNORE:
				case RCV_BAD:
					break;
		}
		if (packet_in.last == 1) {
			if ((rcv_state == RCV_GOOD) || (rcv_state == RCV_BAD)) packets_read++;
			if ((rcv_state == RCV_BAD) ||
					((rcv_state == RCV_GOOD) && (packet_in.user == 1)) ||
					((rcv_state == RCV_GOOD) && (o_idx != 130))) {
				act_reg->Data.bad_packets++;
				act_reg->Data.user += packet_in.user;
				o_idx = 0; // offset stays the same, so bad data will be overwritten
			} else if (rcv_state == RCV_GOOD) {
				act_reg->Data.good_packets++;
				out_offset += 130;
				o_idx = 0;
			}
			rcv_state = RCV_INIT;
		}
	}

	act_reg->Data.read_size = bytes_read;

	act_reg->Control.Retc = SNAP_RETC_SUCCESS;
	return 0;
}

//--- TOP LEVEL MODULE -------------------------------------------------
void hls_action(snap_membus_t *din_gmem,
		snap_membus_t *dout_gmem,
		AXI_STREAM &din_eth,
		/* snap_membus_t *d_ddrmem, // CAN BE COMMENTED IF UNUSED */
		action_reg *act_reg,
		action_RO_config_reg *Action_Config)
{
	// Host Memory AXI Interface - CANNOT BE REMOVED - NO CHANGE BELOW
#pragma HLS INTERFACE m_axi port=din_gmem bundle=host_mem offset=slave depth=512 \
		max_read_burst_length=64  max_write_burst_length=64
#pragma HLS INTERFACE s_axilite port=din_gmem bundle=ctrl_reg offset=0x030

#pragma HLS INTERFACE m_axi port=dout_gmem bundle=host_mem offset=slave depth=512 \
		max_read_burst_length=64  max_write_burst_length=64
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

#pragma HLS INTERFACE axis register off port=din_eth
	// #pragma HLS INTERFACE axis register off port=dout_eth

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
		process_action(din_gmem, dout_gmem, din_eth, act_reg);
		break;
	}
}

