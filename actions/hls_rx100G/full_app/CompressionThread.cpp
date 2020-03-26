#include <unistd.h>
#include <malloc.h>
#include <iostream>
#include <arpa/inet.h>

#include "JFReceiver.h"
#include "IB_Transport.h"

// Taken from bshuf
/* Write a 64 bit unsigned integer to a buffer in big endian order. */
void bshuf_write_uint64_BE(void* buf, uint64_t num) {
    int ii;
    uint8_t* b = (uint8_t*) buf;
    uint64_t pow28 = 1 << 8;
    for (ii = 7; ii >= 0; ii--) {
        b[ii] = num % pow28;
        num = num / pow28;
    }
}
/* Write a 32 bit unsigned integer to a buffer in big endian order. */
void bshuf_write_uint32_BE(void* buf, uint32_t num) {
    int ii;
    uint8_t* b = (uint8_t*) buf;
    uint32_t pow28 = 1 << 8;
    for (ii = 3; ii >= 0; ii--) {
        b[ii] = num % pow28;
        num = num / pow28;
    }
}

// block size must be multiple of 16 and is in units of 4-bytes
size_t byte_shuffle_and_compress_block32(char *out, uint32_t *in, size_t block_size) {
    // TODO: Assert block_size % 16 == 0
    size_t compressed_size = 0L;
    for (int i = 0; i < NPIXEL * 2 /(block_size *sizeof(uint64_t)) ; i++) {
        uint32_t tmp[block_size];
        // data comes already ordered as 512-bit subblocks (32x16)
        // first uint32_t in subblock has most signifianct bits of all numbers belonging to the subblock
        for (int i = 0; i < block_size; i++) {
            size_t subblock = i/16; // as i <= block_size, than subblock <= block_size / 16
            size_t pos_in_subblock = i%16;
            size_t new_pos = pos_in_subblock * (block_size / 16) + subblock; 
            tmp[new_pos] = in[i];
        }

        size_t block_compressed_size = 
               LZ4_compress_default((char *)(tmp), out+compressed_size+4, block_size*sizeof(uint32_t), RDMA_BUFFER_MAX_ELEM_SIZE);
        bshuf_write_uint32_BE(out+compressed_size, block_compressed_size);
        compressed_size += block_compressed_size + 4;
    }
    return compressed_size;
}

// block size must be multiple of 16 and is in units of 8-bytes
size_t byte_shuffle_and_compress_block64(char *out, uint64_t *in, size_t block_size) {
    // TODO: Assert block_size % 16 == 0
    size_t compressed_size = 0L;
    for (int i = 0; i < NPIXEL * 2 /(block_size *sizeof(uint64_t)) ; i++) {
        uint64_t tmp[block_size];
        // data comes already ordered as 1024-bit subblocks (64x16)
        // first uint64_t in subblock has most signifianct bits of all numbers belonging to the subblock
        for (int i = 0; i < block_size; i++) {
            size_t subblock = i/16; // as i <= block_size, than subblock <= block_size / 16
            size_t pos_in_subblock = i%16;
            size_t new_pos = pos_in_subblock * (block_size / 16) + subblock; 
            tmp[new_pos] = in[i];
        }

        size_t block_compressed_size = 
               LZ4_compress_default((char *)(tmp), out+compressed_size+4, block_size*sizeof(uint64_t), RDMA_BUFFER_MAX_ELEM_SIZE);
        bshuf_write_uint32_BE(out+compressed_size, block_compressed_size);
        compressed_size += block_compressed_size + 4;
    }
    return compressed_size;
}

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

uint32_t LZ4_compress_frame(size_t frame, size_t buffer_id) {
	uint32_t block_size = NPIXEL * 2;

	// Compress frame with LZ4 and save to outgoing buffer
	uint32_t compressed_size = LZ4_compress_default((char *)(frame_buffer + ((trigger_frame + frame) % FRAME_BUF_SIZE) * NPIXEL * sizeof(int16_t)),
			ib_buffer + RDMA_BUFFER_MAX_ELEM_SIZE,
			block_size, RDMA_BUFFER_MAX_ELEM_SIZE) ;

	// Save metadata - frame number and compressed size (uncompressed size and block size are trivial)
	//bshuf_write_uint64_BE(ib_buffer + RDMA_BUFFER_MAX_ELEM_SIZE * buffer_id, frame);
	//bshuf_write_uint32_BE(ib_buffer + RDMA_BUFFER_MAX_ELEM_SIZE * buffer_id + 8, compressed_size);

	return compressed_size;
}

void *send_thread(void *in_threadarg) {
    ThreadArg *arg = (ThreadArg *) in_threadarg;

    std::cout << "Starting thread #" << arg->ThreadID << std::endl;

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
    	// Find free buffer
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

    	// Ensure that frame was already collected, if not wait 2 ms to try again
    	while (lastModuleFrameNumber() < trigger_frame + frame + 5) {
           usleep(2000);
        }
    	// Copy frame to outgoing buffer (later on - compress)
        
    	// memcpy(ib_buffer + buffer_id * RDMA_BUFFER_MAX_ELEM_SIZE, frame_buffer + ((trigger_frame + frame) % FRAME_BUF_SIZE) * NPIXEL , NPIXEL * sizeof(uint16_t));

    	// Send the frame via RDMA
    	ibv_sge ib_sg;
    	ibv_send_wr ib_wr;
    	ibv_send_wr *ib_bad_wr;

    	memset(&ib_sg, 0, sizeof(ib_sg));
    	ib_sg.addr	 = (uintptr_t)(ib_buffer + RDMA_BUFFER_MAX_ELEM_SIZE * buffer_id);

        if (experiment_settings.conversion_mode == MODE_CONV_BSHUF) {
            ib_sg.length     = byte_shuffle_and_compress_block64(
				ib_buffer + RDMA_BUFFER_MAX_ELEM_SIZE * buffer_id,
				(uint64_t *)frame_buffer + ((trigger_frame + frame) % FRAME_BUF_SIZE) * NPIXEL,
                                512);
        } else {
            memcpy(ib_buffer + RDMA_BUFFER_MAX_ELEM_SIZE * buffer_id,
                   frame_buffer + ((trigger_frame + frame) % FRAME_BUF_SIZE) * NPIXEL,
                   NPIXEL * sizeof(uint16_t));
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
