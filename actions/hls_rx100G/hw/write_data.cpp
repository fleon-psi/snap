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

void write_data(DATA_STREAM &in, snap_membus_t *dout_gmem, size_t mem_offset, uint64_t &bytes_written) {
	data_packet_t packet_in;
	in.read(packet_in);

	int counter = 0;
	while (packet_in.exit == 0) {
#pragma HLS PIPELINE
		// TODO: accounting which packets were converted
		size_t offset = packet_in.frame_number * (NMODULES * MODULE_COLS * MODULE_LINES / 32) +
				packet_in.module * (MODULE_COLS * MODULE_LINES/32) +
				packet_in.eth_packet * (4096/32)
				+ packet_in.axis_packet;
//		memcpy(dout_gmem + (act_reg->Data.out.addr >> 6) + offset, (char *) (&packet_in.data), BPERDW);
		memcpy(dout_gmem + mem_offset + counter, (char *) (&packet_in.data), BPERDW);
		counter++;

		in.read(packet_in);
	}
	bytes_written = counter * 64;
}
