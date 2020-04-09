/*
 * Copyright 2020 Paul Scherrer Institute
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

#include <unistd.h>
#include <malloc.h>
#include <iostream>
#include <arpa/inet.h>

#include "JFReceiver.h"
#include "IB_Transport.h"

uint32_t lastModuleFrameNumber() {
    uint32_t retVal = online_statistics->head[0];
    for (int i = 1; i < NMODULES; i++) {
        if (online_statistics->head[i] < retVal) retVal = online_statistics->head[i];
    }
    return retVal;
}

void *poll_cq_thread(void *in_threadarg) {
	for (size_t finished_wc = 0; finished_wc < experiment_settings.nframes_to_write; finished_wc++) {
		// Poll CQ to reuse ID
		ibv_wc ib_wc;
		int num_comp = 0; // number of completions present in the CQ
		while (num_comp == 0) {
			num_comp = ibv_poll_cq(ib_settings.cq, 1, &ib_wc);

			if (num_comp < 0) {
				std::cerr << "Failed polling IB Verbs completion queue" << std::endl;
				pthread_exit(0);
			}

			if (ib_wc.status != IBV_WC_SUCCESS) {
				std::cerr << "Failed status " << ibv_wc_status_str(ib_wc.status) << " of IB Verbs send request #" << (int)ib_wc.wr_id << std::endl;
				pthread_exit(0);
			}
			usleep(100);
		}

		pthread_mutex_lock(&ib_buffer_occupancy_mutex);
		ib_buffer_occupancy[ib_wc.wr_id] = 0;
		pthread_cond_signal(&ib_buffer_occupancy_cond);
		pthread_mutex_unlock(&ib_buffer_occupancy_mutex);
	}
	pthread_exit(0);
}

void *send_thread(void *in_threadarg) {
    ThreadArg *arg = (ThreadArg *) in_threadarg;

    pthread_mutex_lock(&trigger_frame_mutex);
    if (arg->ThreadID == 0) {
    	while (online_statistics->trigger_position < experiment_settings.pedestalG0_frames) usleep(1000);
    	trigger_frame = online_statistics->trigger_position;
    	pthread_cond_broadcast(&trigger_frame_cond);
    	std::cout << "Trigger observed at" << trigger_frame << std::endl;
    } else {
    	while (online_statistics->trigger_position < experiment_settings.pedestalG0_frames)
    		pthread_cond_wait(&trigger_frame_cond, &trigger_frame_mutex);
    }
    pthread_mutex_unlock(&trigger_frame_mutex);

    for (size_t frame = arg->ThreadID;
    		frame < experiment_settings.nframes_to_write;
    		frame += receiver_settings.compression_threads) {

    	// Find free buffer to write
    	int32_t buffer_id = -1;

    	pthread_mutex_lock(&ib_buffer_occupancy_mutex);
    	do  {
    		int i = 0;
    		while ((ib_buffer_occupancy[i] != 0) && (i < RDMA_SQ_SIZE - 1)) {
    			i++;
    		}
    		if (i < RDMA_SQ_SIZE - 1) {
    			ib_buffer_occupancy[i] = frame;
    			buffer_id = i;
    		} else pthread_cond_wait(&ib_buffer_occupancy_cond, &ib_buffer_occupancy_mutex);
    	} while (buffer_id == -1);
    	pthread_mutex_unlock(&ib_buffer_occupancy_mutex);

        size_t collected_frame = frame*experiment_settings.summation - trigger_frame;

    	// Ensure that frame was already collected, if not wait 1 ms to try again
    	while (lastModuleFrameNumber() < collected_frame + 5) {
           usleep(1000);
        }
        if (experiment_settings.conversion_mode == MODE_CONV) {
          if (experiment_settings.summation == 1) {
            // For summation of 1 buffer is 16-bit, otherwise 32-bit
            int16_t *output_buffer = (int16_t *) (ib_buffer + RDMA_BUFFER_MAX_ELEM_SIZE * buffer_id);
            // Correct geometry to make 2x2 module arrangement
            // Add inter-chip gaps
            // Inter module gaps are not added and should be corrected in processing software
            // TODO: Double pixels
            for (int module = 0; module < NMODULES; module ++) {
                for (uint64_t line = 0; line < MODULE_LINES; line ++) {
                    size_t pixel_in = (collected_frame % FRAME_BUF_SIZE) * NPIXEL + (module * MODULE_LINES + line) * MODULE_COLS;
                    size_t line_out = 513L - line - (line / 256) * 2L;
                    if (module < 2) line_out += 514; // Modules 2 and 3 are on the top
                    line_out += module % 2; // Modules are interleaved to make 2 modules in one line
                    for (uint64_t column = 0; column < MODULE_COLS; column ++) {
                        size_t pixel_out = line_out * 1030 + column + (column / 256) * 2L;
                        output_buffer[pixel_out] = frame_buffer[pixel_in + column];
                    }
                }
            }
          } else {
            // For summation of >= 2 32-bit integers are used
            int32_t *output_buffer = (int32_t *) (ib_buffer + RDMA_BUFFER_MAX_ELEM_SIZE * buffer_id);
            for (int module = 0; module < NMODULES; module ++) {
                for (uint64_t line = 0; line < MODULE_LINES; line ++) {
                    size_t pixel_in = (collected_frame % FRAME_BUF_SIZE) * NPIXEL + (module * MODULE_LINES + line) * MODULE_COLS;
                    size_t line_out = 513L - line - (line / 256) * 2L;
                    if (module < 2) line_out += 514; // Modules 2 and 3 are on the top
                    line_out += module % 2; // Modules are interleaved to make 2 modules in one line
                    for (uint64_t column = 0; column < MODULE_COLS; column ++) {
                        size_t pixel_out = line_out * 1030 + column + (column / 256) * 2L;
                        output_buffer[pixel_out] = frame_buffer[pixel_in + column];
                    }
                }
            }
            // Add summed frames
            for (uint32_t i = 1; i < experiment_settings.summation; i++) {
                collected_frame++;

    	        while (lastModuleFrameNumber() < collected_frame + 5) {
                    std::cout << "Frame " << collected_frame << " not found " << lastModuleFrameNumber() << std::endl;
                    usleep(1000);
                }
                for (int module = 0; module < NMODULES; module ++) {
                    for (size_t line = 0; line < MODULE_LINES; line ++) {
                        size_t pixel_in = (collected_frame % FRAME_BUF_SIZE) * NPIXEL + (module * MODULE_LINES + line) * MODULE_COLS;
                        size_t line_out = 513L - line - (line / 256) * 2L;
                        if (module < 2) line_out += 514; // Modules 2 and 3 are on the top
                        line_out += module % 2; // Modules are interleaved to make 2 modules in one line
                        for (size_t column = 0; column < MODULE_COLS; column ++) {
                            size_t pixel_out = line_out * 1030 + column + (column / 256) * 2L;
                            output_buffer[pixel_out] += frame_buffer[pixel_in + column];
                        }
                    }
                }
            }
          }
        } else {
            memcpy(ib_buffer + RDMA_BUFFER_MAX_ELEM_SIZE * buffer_id, frame_buffer + (collected_frame % FRAME_BUF_SIZE) * NPIXEL, NPIXEL * sizeof(uint16_t));
        }

    	// Send the frame via RDMA
    	ibv_sge ib_sg;
    	ibv_send_wr ib_wr;
    	ibv_send_wr *ib_bad_wr;

    	memset(&ib_sg, 0, sizeof(ib_sg));
    	ib_sg.addr	 = (uintptr_t)(ib_buffer + RDMA_BUFFER_MAX_ELEM_SIZE * buffer_id);
        if (experiment_settings.conversion_mode == MODE_CONV) {
            if (experiment_settings.summation == 1)
                ib_sg.length = 514 * 1030 * NMODULES * sizeof(int16_t);
            else
                ib_sg.length = 514 * 1030 * NMODULES * sizeof(int32_t);
        } else {
                ib_sg.length = NPIXEL * sizeof(uint16_t);
        }

        ib_sg.lkey	 = ib_settings.buffer_mr->lkey;

    	memset(&ib_wr, 0, sizeof(ib_wr));
    	ib_wr.wr_id      = buffer_id;
    	ib_wr.sg_list    = &ib_sg;
    	ib_wr.num_sge    = 1;
    	ib_wr.opcode     = IBV_WR_SEND_WITH_IMM;
    	ib_wr.send_flags = IBV_SEND_SIGNALED;
        ib_wr.imm_data   = htonl(frame); // Network order

    	if (ibv_post_send(ib_settings.qp, &ib_wr, &ib_bad_wr)) {
    		std::cerr << "Sending with IB Verbs failed (buffer:" << buffer_id << ")" << std::endl;
    		pthread_exit(0);
    	}
    }
    pthread_exit(0);
}
