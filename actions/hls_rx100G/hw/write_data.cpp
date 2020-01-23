/*
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

#include "hw_action_rx100G.h"

void write_data(DATA_STREAM &in, snap_membus_t *dout_gmem, size_t in_gain_pedestal_addr, size_t out_frame_buffer_addr, size_t out_frame_status_addr) {
	data_packet_t packet_in;
	in.read(packet_in);

	int counter_ok = 0;
	int counter_wrong = 0;

	uint64_t head[NMODULES]; // number of the newest packet received for the frame

	for (int i = 0; i < NMODULES; i++) head[i] = 0;

	while (packet_in.exit == 0) {
		while ((packet_in.exit == 0) && (packet_in.axis_packet == 0)) {
			// TODO: accounting which packets were converted
#pragma HLS PIPELINE II=128
			size_t out_frame_addr = out_frame_buffer_addr +
					       (packet_in.frame_number % FRAME_BUF_SIZE) * (NMODULES * MODULE_COLS * MODULE_LINES / 32) +
							packet_in.module * (MODULE_COLS * MODULE_LINES/32) +
							packet_in.eth_packet * (4096/32);

			bool frame_ok = true;

			ap_uint<512> buffer[128];

			uint64_t frame_number0 = packet_in.frame_number;
			ap_uint<4> module0 = packet_in.module;
			ap_uint<8> eth_packet0 = packet_in.eth_packet;

			bool is_head = true;

			if (packet_in.frame_number > head[packet_in.module]) {
				for (int i = 0; i < NMODULES; i++)
					if (head[i] >= packet_in.frame_number) is_head = false;
				head[packet_in.module] = packet_in.frame_number;
			}

			ap_uint<1> last_axis_user;

			for (int i = 0; i < 128; i++) {
				if (i == 127) last_axis_user = packet_in.axis_user; // relevant for the last packet
				buffer[i] = packet_in.data;
				in.read(packet_in);
			}

			memcpy(dout_gmem + out_frame_addr, buffer, 128*64);

			if (((packet_in.axis_packet == 0) && (last_axis_user == 0)) || (packet_in.exit == 1)) {
				counter_ok++;
				// frame_status[packet_in.module * FRAME_STATUS_BUF_SIZE + (packet_in.frame_number % FRAME_STATUS_BUF_SIZE)]++;
			}

			else counter_wrong++;
		}
		while ((packet_in.exit == 0) && (packet_in.axis_packet != 0)) {
			// forward, to get to a beginning of a meaningful packet.
			in.read(packet_in);
		}
	}
	ap_uint<512> statistics;
	statistics(31,0) = counter_ok;
	statistics(63,32) = counter_wrong;
	statistics(127,64) = packet_in.frame_number;

	// memcpy(dout_gmem+mem_offset, (char *) &statistics, BPERDW);
}
