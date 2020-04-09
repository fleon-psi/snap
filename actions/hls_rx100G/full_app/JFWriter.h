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

#ifndef JFWRITER_H_
#define JFWRITER_H_

#include <hdf5.h>
#include "JFApp.h"

#define RDMA_RQ_SIZE 256L // Maximum number of receive elements
#define NCARDS       2

// HDF5 - data file (should be array for multiple files)
hid_t data_file;
hid_t data_group;
hid_t data_dataspace;
hid_t data_dataset;
hid_t data_dcpl;

enum compression_t {COMPRESSION_NONE, COMPRESSION_BSHUF_LZ4, COMPRESSION_BSHUF_ZSTD};

// Settings only necessary for writer
struct writer_settings_t {
	std::string HDF5_prefix;
	int images_per_file;
	int nthreads;
	int nlocations;
	std::string data_location[256];
	std::string main_location;
        compression_t compression; 
};
extern writer_settings_t writer_settings;

struct writer_connection_settings_t {
	int sockfd;                 // TCP/IP socket
	std::string receiver_host;  // Receiver host
	uint16_t receiver_tcp_port; // Receiver TCP port
	std::string ib_dev_name;    // IB device name
	ib_settings_t ib_settings;  // IB settings
	char *ib_buffer;            // IB buffer
	ibv_mr *ib_buffer_mr;       // IB buffer memory region for Verbs
};

// Thread information
struct writer_thread_arg_t {
	uint16_t thread_id;
	uint16_t card_id;
};

void *writer_thread(void* threadArg);

struct gain_pedestal_t {
	uint16_t gainG0[NCARDS*NPIXEL];
	uint16_t gainG1[NCARDS*NPIXEL];
	uint16_t gainG2[NCARDS*NPIXEL];
	uint16_t pedeG1[NCARDS*NPIXEL];
	uint16_t pedeG2[NCARDS*NPIXEL];
	uint16_t pedeG0[NCARDS*NPIXEL];
};

extern gain_pedestal_t gain_pedestal;
extern online_statistics_t online_statistics[NCARDS];

extern experiment_settings_t experiment_settings;
extern writer_connection_settings_t writer_connection_settings[NCARDS];

extern uint8_t writers_done_per_file;
extern pthread_mutex_t writers_done_per_file_mutex;
extern pthread_cond_t writers_done_per_file_cond;

#endif // JFWRITER_H_
