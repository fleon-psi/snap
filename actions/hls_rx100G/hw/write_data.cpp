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

#define STATUS_BUFFER_SIZE 4

struct packet_counter_t
{
	uint64_t head;
	ap_uint<8> counter[64];
};

void write_data(DATA_STREAM &in, snap_membus_t *dout_gmem,
		size_t in_gain_pedestal_addr, size_t out_frame_buffer_addr,
		size_t out_frame_status_addr,
		snap_membus_t *d_hbm_p0, snap_membus_t *d_hbm_p1,
		snap_membus_t *d_hbm_p2, snap_membus_t *d_hbm_p3,
		snap_membus_t *d_hbm_p4, snap_membus_t *d_hbm_p5,
		snap_membus_t *d_hbm_p6, snap_membus_t *d_hbm_p7) {
	data_packet_t packet_in;
	in.read(packet_in);

	int counter_ok = 0;
	int counter_wrong = 0;

	packed_pedeG0_t pedestal_G0[NPIXEL/32];
#pragma HLS RESOURCE variable=pedestal_G0 core=RAM_1P_URAM
#pragma HLS ARRAY_PARTITION variable=pedestal_G0 cyclic factor=8 dim=1

	ap_uint<32> mask[NPIXEL/32];

	packet_counter_t packet_counter[STATUS_BUFFER_SIZE];
	Initialize_status: for (int i = 0; i < STATUS_BUFFER_SIZE; i++) {
#pragma HLS PIPELINE II=32
		packet_counter[i].head = i*64;
		for (int j = 0; j < 64; j++) {
			packet_counter[i].counter[j] = 0;
		}
	}

	uint64_t head[NMODULES]; // number of the newest packet received for the frame
	for (int i = 0; i < NMODULES; i++) {
#pragma HLS UNROLL
		head[i] = 0;
	}

	while (packet_in.exit == 0) {
		Loop_good_packet: while ((packet_in.exit == 0) && (packet_in.axis_packet == 0)) {
			// TODO: accounting which packets were converted
#pragma HLS PIPELINE II=130
			size_t out_frame_addr = out_frame_buffer_addr +
					       (packet_in.frame_number % FRAME_BUF_SIZE) * (NMODULES * MODULE_COLS * MODULE_LINES / 32) +
							packet_in.module * (MODULE_COLS * MODULE_LINES/32) +
							packet_in.eth_packet * (4096/32);




			// 1. Extract information about the frame
			uint64_t frame_number0 = packet_in.frame_number;
			ap_uint<4> module0 = packet_in.module;
			ap_uint<8> eth_packet0 = packet_in.eth_packet;

			size_t packet_counter_addr = (module0 + frame_number0 * NMODULES) % (64 * STATUS_BUFFER_SIZE);

			bool is_head = false;

			if (packet_in.frame_number > head[packet_in.module]) {
				is_head = true;
				for (int i = 0; i < NMODULES; i++)
					if (head[i] >= packet_in.frame_number) is_head = false;

				head[packet_in.module] = packet_in.frame_number;

				ap_uint<512> statistics;
			    statistics(31,0) = counter_ok;
				statistics(63,32) = counter_wrong;
				statistics(127,64) = packet_in.frame_number;
				for (int i = 0; i < NMODULES; i++) {
					statistics(128 + i * 64 + 63, 128 + i * 64) = head[i];
				}
				memcpy(dout_gmem+out_frame_status_addr, (char *) &statistics, BPERDW);

			}

			// 2. Load conversion constants
			size_t offset = packet_in.module * (MODULE_COLS * MODULE_LINES/32) + packet_in.eth_packet * (4096/32);

			ap_uint<512> packed_pedeG0RMS[128];
#pragma HLS RESOURCE variable=packed_pede_G0RMS core=RAM_2P_URAM
			ap_uint<512> packed_pedeG1[128];
#pragma HLS RESOURCE variable=packed_pede_G1 core=RAM_2P_URAM
			ap_uint<512> packed_pedeG2[128];
#pragma HLS RESOURCE variable=packed_pede_G2 core=RAM_2P_URAM
			ap_uint<512> packed_gainG0[128];
#pragma HLS RESOURCE variable=packed_gain_G0 core=RAM_2P_URAM
			ap_uint<512> packed_gainG1[128];
#pragma HLS RESOURCE variable=packed_gain_G1 core=RAM_2P_URAM
			ap_uint<512> packed_gainG2[128];
#pragma HLS RESOURCE variable=packed_gain_G2 core=RAM_2P_URAM

			memcpy(packed_pedeG1, d_hbm_p0+offset, 64*128);
			memcpy(packed_pedeG2, d_hbm_p1+offset, 64*128);
			memcpy(packed_gainG0, d_hbm_p2+offset, 64*128);
			memcpy(packed_gainG1, d_hbm_p3+offset, 64*128);
			memcpy(packed_gainG2, d_hbm_p4+offset, 64*128);
			memcpy(packed_pedeG0RMS, d_hbm_p5+offset, 64*128);

			// 3. Load frames and convert
			ap_uint<1> last_axis_user;
			ap_uint<512> buffer[128];
#pragma HLS RESOURCE variable=buffer core=RAM_2P_URAM
			for (int i = 0; i < 128; i++) {
				if (i == 127) last_axis_user = packet_in.axis_user; // relevant for the last packet

				convert_and_shuffle(packet_in.data, buffer[i], pedestal_G0[offset + i], packed_pedeG0RMS[i], packed_gainG0[i], packed_pedeG1[i], packed_gainG1[i], packed_pedeG2[i], packed_gainG2[i]);

				in.read(packet_in);
			}

			// 4. Transfer data out
			memcpy(dout_gmem + out_frame_addr, buffer, 128*64);

			// 5. Calculate statistics and transfer them to host memory

			// If this is first frame with this number AND it starts new 64
			if ((is_head) && ((frame_number0 * NMODULES) % 64 == 0)) {
				// Save data from old counter to host memory, before new buffer can be filled
				// But - if this is first time buffer is filled, there is no data to be saved
				if (packet_counter[packet_counter_addr/64].head != frame_number0) {
					ap_uint<512> tmp;
					for (int i = 0; i < 512; i++) tmp[i] = packet_counter[packet_counter_addr/64].counter[i/8][i%8];
					memcpy(dout_gmem + out_frame_status_addr + 1 + ((packet_counter[packet_counter_addr/64].head * NMODULES)/ 64), &tmp, 64);
					packet_counter[packet_counter_addr/64].head = frame_number0;
				}
				// Start a new packet counter
				for (int i = 0; i < 64; i++) packet_counter[packet_counter_addr/64].counter[i] = 0;
			}
			// Check if frame is OK
			if (((packet_in.axis_packet == 0) || (packet_in.exit == 1))  && (last_axis_user == 0)) {
				counter_ok++;
				packet_counter[packet_counter_addr / 64].counter[packet_counter_addr % 64]++;
			} else counter_wrong++;
		}
		Loop_err_packet: while ((packet_in.exit == 0) && (packet_in.axis_packet != 0)) {
			// Forward, to get to a beginning of a meaningful packet.
			in.read(packet_in);
		}
	}

	// Save statistics that are in the packet_counter
	Save_remainder: for (int i = 0; i < STATUS_BUFFER_SIZE; i++) {
#pragma HLS PIPELINE II=32
		ap_uint<512> tmp;
		for (int j = 0; j < 512; j++) tmp[j] = packet_counter[i].counter[j/8][j%8];
		memcpy(dout_gmem + out_frame_status_addr + 1 + (packet_counter[i].head * NMODULES / 64), &tmp, 64);
	}


}
