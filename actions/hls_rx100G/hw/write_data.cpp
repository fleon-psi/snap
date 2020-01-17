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

void write_data(DATA_STREAM &in, snap_membus_t *dout_gmem, size_t mem_offset) {
	data_packet_t packet_in;
	in.read(packet_in);

	int counter_ok = 0;
	int counter_wrong = 0;


	bool exit_condition = false;

	while (packet_in.exit == 0) {
		// TODO: accounting which packets were converted
		// TODO: what if only part of packet arrives?
#pragma HLS PIPELINE II=128
		//size_t offset = packet_in.frame_number * (NMODULES * MODULE_COLS * MODULE_LINES / 32) +
		//				packet_in.module * (MODULE_COLS * MODULE_LINES/32) +
		//				packet_in.eth_packet * (4096/32)
		//				+ packet_in.axis_packet;
		bool frame_ok = true;

		ap_uint<512> buffer[128];

		size_t offset = mem_offset + counter_ok * 128 + 1;

		for (int i = 0; i < 128; i++) {
			buffer[i] = packet_in.data;
			in.read(packet_in);
		}
		if ((packet_in.axis_packet == 0) || (packet_in.exit == 1)) counter_ok++;
		else counter_wrong++;

		memcpy(dout_gmem + offset, buffer, 128*64);
	}
	ap_uint<512> statistics;
	statistics(31,0) = counter_ok;
	statistics(63,32) = counter_wrong;
	statistics(127,64) = packet_in.frame_number;
	memcpy(dout_gmem+mem_offset, (char *) &statistics, BPERDW);
}
