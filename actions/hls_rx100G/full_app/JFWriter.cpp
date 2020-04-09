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

#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <unistd.h>
#include <sys/mman.h>
#include <algorithm>
#include <hdf5.h>
#include <chrono>

#include <endian.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>

#include "JFWriter.h"
#include "bitshuffle/bitshuffle.h"

#define LZ4_BLOCK_SIZE  0
#define ZSTD_BLOCK_SIZE (8*514*1030)

extern "C" {
extern H5Z_class_t H5Z_JF[1];
}

writer_settings_t writer_settings;
gain_pedestal_t gain_pedestal;
online_statistics_t online_statistics[NCARDS];

experiment_settings_t experiment_settings;
writer_connection_settings_t writer_connection_settings[NCARDS];

uint8_t writers_done_per_file;
pthread_mutex_t writers_done_per_file_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t writers_done_per_file_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t hdf5_mutex = PTHREAD_MUTEX_INITIALIZER;

size_t total_compressed_size = 0;
pthread_mutex_t total_compressed_size_mutex = PTHREAD_MUTEX_INITIALIZER;

uint64_t remaining_frames[NCARDS];
pthread_mutex_t remaining_frames_mutex[NCARDS];


// Taken from bshuf
extern "C" {
    void bshuf_write_uint64_BE(void* buf, uint64_t num); 
    void bshuf_write_uint32_BE(void* buf, uint32_t num);
}

int addStringAttribute(hid_t location, std::string name, std::string val) {
	/* https://support.hdfgroup.org/ftp/HDF5/current/src/unpacked/examples/h5_attribute.c */
	hid_t aid = H5Screate(H5S_SCALAR);
	hid_t atype = H5Tcopy(H5T_C_S1);
	H5Tset_size(atype, val.length());
	H5Tset_strpad(atype,H5T_STR_NULLTERM);
	hid_t attr = H5Acreate2(location, name.c_str(), atype, aid, H5P_DEFAULT, H5P_DEFAULT);
	herr_t ret = H5Awrite(attr, atype, val.c_str());
	ret = H5Sclose(aid);
	ret = H5Tclose(atype);
	ret = H5Aclose(attr);

	return 0;
}

int addDoubleAttribute(hid_t location, std::string name, const double *val, int dim) {
    // https://support.hdfgroup.org/ftp/HDF5/current/src/unpacked/examples/h5_crtdat.c
    hsize_t dims[1];
    dims[0] = dim;

    /* Create the data space for the dataset. */
    hid_t dataspace_id = H5Screate_simple(1, dims, NULL);

    hid_t attr = H5Acreate2(location, name.c_str(), H5T_IEEE_F64LE, dataspace_id, H5P_DEFAULT, H5P_DEFAULT);

    herr_t ret = H5Awrite(attr, H5T_NATIVE_DOUBLE, val);

    ret = H5Sclose(dataspace_id);
    ret = H5Aclose(attr);

    return 0;
}

hid_t createGroup(hid_t master_file_id, std::string group, std::string nxattr) {
	hid_t group_id = H5Gcreate(master_file_id, group.c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	if (nxattr != "") addStringAttribute(group_id, "NX_class", nxattr);
	return group_id;
}

int saveUInt16_3D(hid_t location, std::string name, const uint16_t *val, int dim1, int dim2, int dim3, double multiplier) {
    herr_t status;

    // https://support.hdfgroup.org/ftp/HDF5/current/src/unpacked/examples/h5_crtdat.c
    hsize_t dims[3];
    dims[0] = dim1;
    dims[1] = dim2;
    dims[2] = dim3;

    // Create the data space for the dataset.
    hid_t dataspace_id = H5Screate_simple(3, dims, NULL);

    // Create the dataset.
    hid_t dataset_id = H5Dcreate2(location, name.c_str(), H5T_STD_U16LE, dataspace_id,
                                  H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

    // Write the dataset.
    status = H5Dwrite(dataset_id, H5T_NATIVE_USHORT, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                      val);

    addDoubleAttribute(dataset_id, "multiplier", &multiplier, 1);

    // End access to the dataset and release resources used by it.
    status = H5Dclose(dataset_id);

    // Terminate access to the data space.
    status = H5Sclose(dataspace_id);

    return 0;
}

int open_HDF5_file(uint32_t file_id) {
	int32_t frames = experiment_settings.nframes_to_write - writer_settings.images_per_file * file_id;
	if (frames >  writer_settings.images_per_file) frames =  writer_settings.images_per_file;
	if (frames <= 0) return 1;

	// generate filename for data file
	char buff[12];
	snprintf(buff,12,"data_%06d", file_id+1);
	std::string filename = writer_settings.HDF5_prefix+"_"+std::string(buff)+".h5";

	// Create data file
	data_file = H5Fcreate(filename.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
	if (data_file < 0) {
		std::cerr << "HDF5 data file error" << std::endl;
		return 1;
	}

	hid_t grp = createGroup(data_file, "/entry", "NXentry");
	H5Gclose(grp);

	data_group = createGroup(data_file, "/entry/data","NXdata");

	herr_t h5ret;

	// https://support.hdfgroup.org/ftp/HDF5/current/src/unpacked/examples/h5_crtdat.c
	hsize_t dims[3], chunk[3];

	dims[0] = frames;
	dims[1] = 514 * NMODULES * NCARDS;
	dims[2] = 1030;

	chunk[0] = 1;
	chunk[1] = 514 * NMODULES;
	chunk[2] = 1030;

	// Create the data space for the dataset.
	data_dataspace = H5Screate_simple(3, dims, NULL);

	data_dcpl = H5Pcreate (H5P_DATASET_CREATE);
	h5ret = H5Pset_chunk (data_dcpl, 3, chunk);

	// Set appropriate compression filter
	// SetCompressionFilter(data_dcpl_id);

	// Create the dataset.
	data_dataset = H5Dcreate2(data_group, "data", H5T_STD_I16LE, data_dataspace,
			H5P_DEFAULT, data_dcpl, H5P_DEFAULT);

	// Add attributes
	int tmp = file_id *  writer_settings.images_per_file + 1;
	hid_t aid = H5Screate(H5S_SCALAR);
	hid_t attr = H5Acreate2(data_dataset, "image_nr_low", H5T_STD_I32LE, aid, H5P_DEFAULT, H5P_DEFAULT);
	h5ret = H5Awrite(attr, H5T_NATIVE_INT, &tmp);
	h5ret = H5Sclose(aid);
	h5ret = H5Aclose(attr);

	tmp = tmp+frames-1;
	aid = H5Screate(H5S_SCALAR);
	attr = H5Acreate2(data_dataset, "image_nr_high", H5T_STD_I32LE, aid, H5P_DEFAULT, H5P_DEFAULT);
	h5ret = H5Awrite(attr, H5T_NATIVE_INT, &tmp);
	h5ret = H5Sclose(aid);
	h5ret = H5Aclose(attr);
	return 0;
}

int save_gain_pedestal_hdf5() {
	// Define filename
	std::string filename = "";
	if (writer_settings.main_location != "") filename =
			writer_settings.main_location + "/" +
			writer_settings.HDF5_prefix + "_calibration.h5";
	else filename = writer_settings.HDF5_prefix + "_calibration.h5";

	// Create data file
	hid_t hdf5_file = H5Fcreate(filename.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
	if (hdf5_file < 0) {
		std::cerr << "Cannot create file: " << filename << std::endl;
		return 1;
	}
	hid_t grp = createGroup(hdf5_file, "/entry", "NXentry");
	H5Gclose(grp);

	grp = createGroup(hdf5_file, "/entry/adu_to_photon","");
	saveUInt16_3D(grp, "G0", gain_pedestal.gainG0, 1024, 512, NMODULES * NCARDS, 1.0/(16384.0*512.0));
	saveUInt16_3D(grp, "G1", gain_pedestal.gainG1, 1024, 512, NMODULES * NCARDS, -1.0/8192);
	saveUInt16_3D(grp, "G2", gain_pedestal.gainG2, 1024, 512, NMODULES * NCARDS, -1.0/8192);
	H5Gclose(grp);

	grp = createGroup(hdf5_file, "/entry/pedestal_in_adu","");
	saveUInt16_3D(grp, "G0", gain_pedestal.pedeG0, 1024, 512, NMODULES * NCARDS, 0.25);
	saveUInt16_3D(grp, "G1", gain_pedestal.pedeG1, 1024, 512, NMODULES * NCARDS, 0.25);
	saveUInt16_3D(grp, "G2", gain_pedestal.pedeG2, 1024, 512, NMODULES * NCARDS, 0.25);
	H5Gclose(grp);

        H5Fclose(hdf5_file);
	return 0;
}

void close_HDF5_file() {
	H5Pclose (data_dcpl);

	/* End access to the dataset and release resources used by it. */
	H5Dclose(data_dataset);

	/* Terminate access to the data space. */
	H5Sclose(data_dataspace);

	H5Gclose(data_group);
	H5Fclose(data_file);
}

int exchange_magic_number(int sockfd) {
	uint64_t magic_number;

	// Receive magic number
	read(sockfd, &magic_number, sizeof(uint64_t));
	// Reply with whatever was received
	send(sockfd, &magic_number, sizeof(uint64_t), 0);
	if (magic_number != TCPIP_CONN_MAGIC_NUMBER) {
		std::cerr << "Mismatch in TCP/IP communication" << std::endl;                
		return 1;
	}
	std::cout << "Magic number OK" << std::endl;
	return 0;
}

int TCP_connect(int &sockfd, std::string hostname, uint16_t port) {
	// Use socket to exchange connection information
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == 0) {
		std::cout << "Socket error" << std::endl;
		return 1;
	}

	addrinfo *host_data;

	char txt_buffer[6];
	snprintf(txt_buffer, 6, "%d", port);

	if (getaddrinfo(hostname.c_str(), txt_buffer,  NULL, &host_data)) {
		std::cout << "Host not found" << std::endl;
		return 1;
	}
	if (host_data == NULL) {
		std::cout << "Host " << hostname << " not found" << std::endl;
		return 1;
	}
	if (connect(sockfd, host_data[0].ai_addr, host_data[0].ai_addrlen) < 0) {
		std::cout << "Cannot connect to server" << std::endl;
		return 1;
	}

	return exchange_magic_number(sockfd);
}

void TCP_exchange_IB_parameters(int sockfd, ib_settings_t &ib_settings, ib_comm_settings_t *remote) {
	ib_comm_settings_t local;
	local.qp_num = ib_settings.qp->qp_num;
	local.dlid = ib_settings.port_attr.lid;

	// Receive parameters
	read(sockfd, remote, sizeof(ib_comm_settings_t));

	// Send parameters
	send(sockfd, &local, sizeof(ib_comm_settings_t), 0);
}

int parse_input(int argc, char **argv) {
	int opt;
	experiment_settings.energy_in_keV      = 12.4;
	experiment_settings.pedestalG0_frames  = 0;
	experiment_settings.conversion_mode    = MODE_CONV;
	experiment_settings.nframes_to_collect = 16384;
	experiment_settings.nframes_to_write   = 0;
        experiment_settings.summation          = 1;

        writer_settings.main_location = "/mnt/zfs";
	writer_settings.HDF5_prefix = "";
	writer_settings.images_per_file = 1000;
        writer_settings.compression        = COMPRESSION_NONE;
        
        writer_settings.nthreads = NCARDS;
        writer_settings.nlocations = 0;

	//These parameters are not changeable at the moment
	writer_connection_settings[0].ib_dev_name = "mlx5_0";
	writer_connection_settings[0].receiver_host = "mx-ic922-1";
	writer_connection_settings[0].receiver_tcp_port = 52320;

	if (NCARDS == 2) {
		writer_connection_settings[1].ib_dev_name = "mlx5_12";
		writer_connection_settings[1].receiver_host = "mx-ic922-1";
		writer_connection_settings[1].receiver_tcp_port = 52321;
	}

	while ((opt = getopt(argc,argv,":E:P:012LZRc:w:f:i:hxT:rsS:R")) != EOF)
		switch(opt)
		{
		case 'E':
			experiment_settings.energy_in_keV = atof(optarg);
			break;
		case 'P':
			experiment_settings.pedestalG0_frames  = atoi(optarg);
			break;
		case '0':
			experiment_settings.conversion_mode = MODE_PEDEG0;
			break;
		case '1':
			experiment_settings.conversion_mode = MODE_PEDEG1;
			break;
		case '2':
			experiment_settings.conversion_mode = MODE_PEDEG2;
			break;
		case 'R':
			experiment_settings.conversion_mode = MODE_RAW;
			break;
		case 'L':
			writer_settings.compression = COMPRESSION_BSHUF_LZ4;
			break;
		case 'Z':
			writer_settings.compression = COMPRESSION_BSHUF_ZSTD;
			break;
		case 'x':
			experiment_settings.conversion_mode = 255;
			break;
		case 'c':
			experiment_settings.nframes_to_collect = atoi(optarg);
			break;
		case 'w':
			experiment_settings.nframes_to_write   = atoi(optarg);
			break;
		case 'f':
			writer_settings.HDF5_prefix = std::string(optarg);
			break;
		case 'i':
			writer_settings.images_per_file = atoi(optarg);
			break;
		case 'S':
			experiment_settings.summation = atoi(optarg);
			break;
		case 'T':
			writer_settings.nthreads = atoi(optarg) * NCARDS;
			break;
		case 'r':
			writer_settings.nlocations = 4;
			writer_settings.data_location[0] = "/mnt/n0ram";
			writer_settings.data_location[1] = "/mnt/n1ram";
			writer_settings.data_location[2] = "/mnt/n2ram";
			writer_settings.data_location[3] = "/mnt/n3ram";
			break;
		case 's':
			writer_settings.nlocations = 4;
			writer_settings.data_location[0] = "/mnt/n0ssd";
			writer_settings.data_location[1] = "/mnt/n1ssd";
			writer_settings.data_location[2] = "/mnt/n2ssd";
			writer_settings.data_location[3] = "/mnt/n3ssd";
			break;
		case ':':
			break;
		case '?':
			break;
		}
        if (experiment_settings.conversion_mode == MODE_RAW) {
            writer_settings.compression = COMPRESSION_NONE; // Raw data are not well compressible
            experiment_settings.summation = 1; // No summation possible
        }
        if ((experiment_settings.conversion_mode == MODE_PEDEG0) || 
            (experiment_settings.conversion_mode == MODE_PEDEG1) ||
            (experiment_settings.conversion_mode == MODE_PEDEG2)) {
            experiment_settings.nframes_to_write = 0; // Don't write frames, when collecting pedestal
            writer_settings.compression = COMPRESSION_NONE; // No compression allowed
            experiment_settings.summation = 1; // No summation possible
        }
	return 0;
}

// Save image data "as is" binary
int write_frame(char *data, size_t size, int frame_id, int thread_id) {
	char buff[12];
	snprintf(buff,12,"%08d_%01d", frame_id, thread_id);
	std::string prefix = "";
	if (writer_settings.nlocations > 0)
		prefix = writer_settings.data_location[frame_id % writer_settings.nlocations] + "/";
	std::string filename = prefix + writer_settings.HDF5_prefix+"_"+std::string(buff) + ".img";
	std::ofstream out_file(filename.c_str(), std::ios::binary | std::ios::out);
	if (!out_file.is_open()) return 1;
	out_file.write(data,size);
	out_file.close();
	return 0;
}

int open_connection_card(int card_id) {
	if (TCP_connect(writer_connection_settings[card_id].sockfd,
			writer_connection_settings[card_id].receiver_host,
			writer_connection_settings[card_id].receiver_tcp_port) == 1)
		return 1;

	// Provide experiment settings to receiver
	send(writer_connection_settings[card_id].sockfd,
			&experiment_settings, sizeof(experiment_settings_t), 0);
	if (experiment_settings.conversion_mode == 255) {
		return 0;
	}

	// Setup Infiniband connection
	setup_ibverbs(writer_connection_settings[card_id].ib_settings,
			writer_connection_settings[card_id].ib_dev_name, 0, RDMA_RQ_SIZE);

	// Exchange information with remote host
	ib_comm_settings_t remote;
	TCP_exchange_IB_parameters(writer_connection_settings[card_id].sockfd,
			writer_connection_settings[card_id].ib_settings, &remote);

	// IB buffer
	writer_connection_settings[card_id].ib_buffer = (char *) calloc(RDMA_RQ_SIZE, RDMA_BUFFER_MAX_ELEM_SIZE);

	if (writer_connection_settings[card_id].ib_buffer == NULL) {
		std::cerr << "Memory allocation error" << std::endl;
		return 1;
	}
	writer_connection_settings[card_id].ib_buffer_mr =
			ibv_reg_mr(writer_connection_settings[card_id].ib_settings.pd,
					writer_connection_settings[card_id].ib_buffer, 
                                   RDMA_RQ_SIZE * RDMA_BUFFER_MAX_ELEM_SIZE, IBV_ACCESS_LOCAL_WRITE);
	if (writer_connection_settings[card_id].ib_buffer_mr == NULL) {
		std::cerr << "Failed to register IB memory region." << std::endl;
		return 1;
	}

	// Post WRs
	// Start receiving
	struct ibv_sge ib_sg_entry;
	struct ibv_recv_wr ib_wr, *ib_bad_recv_wr;

	// pointer to packet buffer size and memory key of each packet buffer
	ib_sg_entry.length = RDMA_BUFFER_MAX_ELEM_SIZE;
	ib_sg_entry.lkey = writer_connection_settings[card_id].ib_buffer_mr->lkey;

	ib_wr.num_sge = 1;
	ib_wr.sg_list = &ib_sg_entry;
	ib_wr.next = NULL;

	for (size_t i = 0; (i < RDMA_RQ_SIZE) && (i < experiment_settings.nframes_to_write); i++)
	{
		ib_sg_entry.addr = (uint64_t)(writer_connection_settings[card_id].ib_buffer + RDMA_BUFFER_MAX_ELEM_SIZE*i);
		ib_wr.wr_id = i;
		ibv_post_recv(writer_connection_settings[card_id].ib_settings.qp,
				&ib_wr, &ib_bad_recv_wr);
	}

	// Switch to ready to receive
	switch_to_rtr(writer_connection_settings[card_id].ib_settings,
			remote.rq_psn, remote.dlid, remote.qp_num);
	std::cout << "IB Ready to receive" << std::endl;

	exchange_magic_number(writer_connection_settings[card_id].sockfd);
	return 0;
}

int close_connection_card(int card_id) {
        if (experiment_settings.conversion_mode == 255) {
                close(writer_connection_settings[card_id].sockfd);
                return 0;
        }

	// Send pedestal, header data and collection statistics
	read(writer_connection_settings[card_id].sockfd,
			&(online_statistics[card_id]), sizeof(online_statistics_t));

	for (int i = 0; i < NPIXEL; i ++)
		read(writer_connection_settings[card_id].sockfd,
				&(gain_pedestal.gainG0[card_id*NPIXEL+i]), sizeof(uint16_t));
	for (int i = 0; i < NPIXEL; i ++)
		read(writer_connection_settings[card_id].sockfd,
				&(gain_pedestal.gainG1[card_id*NPIXEL+i]), sizeof(uint16_t));
	for (int i = 0; i < NPIXEL; i ++)
		read(writer_connection_settings[card_id].sockfd,
				&(gain_pedestal.gainG2[card_id*NPIXEL+i]), sizeof(uint16_t));
	for (int i = 0; i < NPIXEL; i ++)
		read(writer_connection_settings[card_id].sockfd,
				&(gain_pedestal.pedeG1[card_id*NPIXEL+i]), sizeof(uint16_t));
	for (int i = 0; i < NPIXEL; i ++)
		read(writer_connection_settings[card_id].sockfd,
				&(gain_pedestal.pedeG2[card_id*NPIXEL+i]), sizeof(uint16_t));
	for (int i = 0; i < NPIXEL; i ++)
		read(writer_connection_settings[card_id].sockfd,
				&(gain_pedestal.pedeG0[card_id*NPIXEL+i]), sizeof(uint16_t));

	// Check magic number again - but don't quit, as the program is finishing anyway soon
	exchange_magic_number(writer_connection_settings[card_id].sockfd);

	// Close IB connection
	ibv_dereg_mr(writer_connection_settings[card_id].ib_buffer_mr);
	close_ibverbs(writer_connection_settings[card_id].ib_settings);

	// Free memory buffer
	free(writer_connection_settings[card_id].ib_buffer);

	// Close TCP/IP socket
	close(writer_connection_settings[card_id].sockfd);

	return 0;
}

void *setup_thread(void* thread_arg) {
	writer_thread_arg_t *arg = (writer_thread_arg_t *)thread_arg;
	int card_id   = arg->card_id; 
        open_connection_card(card_id);
        pthread_exit(0);
}

void *writer_thread(void* thread_arg) {
	writer_thread_arg_t *arg = (writer_thread_arg_t *)thread_arg;
	int thread_id = arg->thread_id;
	int card_id   = arg->card_id;

	// Work request
	// Start receiving
	struct ibv_sge ib_sg_entry;
	struct ibv_recv_wr ib_wr, *ib_bad_recv_wr;

	// pointer to packet buffer size and memory key of each packet buffer
	ib_sg_entry.length = RDMA_BUFFER_MAX_ELEM_SIZE;
	ib_sg_entry.lkey   = writer_connection_settings[card_id].ib_buffer_mr->lkey;

	ib_wr.num_sge = 1;
	ib_wr.sg_list = &ib_sg_entry;
	ib_wr.next = NULL;

        // If no summation, one element is 16-bit, otherwise 32-bit
        size_t elem_size;
        if (experiment_settings.summation == 1) elem_size = 2;
        else elem_size = 4;

        // Create buffer to store compression settings
        char *compression_buffer;
        if (writer_settings.compression == COMPRESSION_BSHUF_LZ4)
            compression_buffer = (char *) malloc(bshuf_compress_lz4_bound(514*1030*NMODULES, elem_size, LZ4_BLOCK_SIZE) + 12);
        if (writer_settings.compression == COMPRESSION_BSHUF_ZSTD)
            compression_buffer = (char *) malloc(bshuf_compress_zstd_bound(514*1030*NMODULES,elem_size, ZSTD_BLOCK_SIZE) + 12);

	// Lock is necessary for calculating loop condition
	pthread_mutex_lock(&remaining_frames_mutex[card_id]);
	// Receive data and write to file
	while (remaining_frames[card_id] > 0) {
		bool repost_wr = false;
		if (remaining_frames[card_id] > RDMA_RQ_SIZE) repost_wr = true;
		remaining_frames[card_id]--;
		pthread_mutex_unlock(&remaining_frames_mutex[card_id]);

		// Poll CQ for finished receive requests
		ibv_wc ib_wc;

		int num_comp;
		do {
			num_comp = ibv_poll_cq(writer_connection_settings[card_id].ib_settings.cq, 1, &ib_wc);

			if (num_comp < 0) {
				std::cerr << "Failed polling IB Verbs completion queue" << std::endl;
				exit(EXIT_FAILURE);
			}

			if (ib_wc.status != IBV_WC_SUCCESS) {
				std::cerr << "Failed status " << ibv_wc_status_str(ib_wc.status) << " of IB Verbs send request #" << (int)ib_wc.wr_id << std::endl;
				exit(EXIT_FAILURE);
			}
			usleep(100);
		} while (num_comp == 0);

		uint32_t frame_id = ntohl(ib_wc.imm_data);
		size_t   frame_size = ib_wc.byte_len;
		char *ib_buffer_location = writer_connection_settings[card_id].ib_buffer
				+ RDMA_BUFFER_MAX_ELEM_SIZE*ib_wc.wr_id;

                switch(writer_settings.compression) {
                    case COMPRESSION_NONE:
		        write_frame(ib_buffer_location, frame_size, frame_id, card_id);
                        break;

                    case COMPRESSION_BSHUF_LZ4:
		        bshuf_write_uint64_BE(compression_buffer, 514*1030*NMODULES * elem_size);
		        bshuf_write_uint32_BE(compression_buffer + 8, LZ4_BLOCK_SIZE);
                        frame_size = bshuf_compress_lz4(ib_buffer_location, compression_buffer + 12, 514*1030*NMODULES, elem_size, LZ4_BLOCK_SIZE) + 12;
		        write_frame(compression_buffer, frame_size, frame_id, card_id);
                        break;

                    case COMPRESSION_BSHUF_ZSTD:
		        bshuf_write_uint64_BE(compression_buffer, 514*1030*NMODULES * elem_size);
		        bshuf_write_uint32_BE(compression_buffer + 8, ZSTD_BLOCK_SIZE);
                        frame_size = bshuf_compress_zstd(ib_buffer_location, compression_buffer + 12, 514*1030*NMODULES, elem_size, ZSTD_BLOCK_SIZE) + 12;
		        write_frame(compression_buffer, frame_size, frame_id, card_id);
                        break;       
                }

		pthread_mutex_lock(&total_compressed_size_mutex);
		total_compressed_size += frame_size;
		pthread_mutex_unlock(&total_compressed_size_mutex);

		// Post new WRs
		if (repost_wr) {
			// Make new work request with the same ID
			// If there is need of new work request
			ib_sg_entry.addr = (uint64_t)(ib_buffer_location);
			ib_wr.wr_id = ib_wc.wr_id;
			ibv_post_recv(writer_connection_settings[card_id].ib_settings.qp, &ib_wr, &ib_bad_recv_wr);
		}
		pthread_mutex_lock(&remaining_frames_mutex[card_id]);
	}
	pthread_mutex_unlock(&remaining_frames_mutex[card_id]);
        free(compression_buffer);
	pthread_exit(0);
}

int main(int argc, char **argv) {
	int ret;

	// Register JF specific filter
	H5Zregister(&H5Z_JF);

	// Parse input
	if (parse_input(argc, argv) == 1) exit(EXIT_FAILURE);

        // Setup connections with two receivers in parallel doesn't work
        // IB Verbs is theoretically thread-safe, but...
//        pthread_t setup[NCARDS];
//        writer_thread_arg_t setup_thread_arg[NCARDS]; 
	for (int i = 0; i < NCARDS; i++) {
//            setup_thread_arg[i].card_id = i;
//	    ret = pthread_create(&(setup[i]), NULL, setup_thread, &(setup_thread_arg[i]));
            open_connection_card(i);
	    remaining_frames[i] = experiment_settings.nframes_to_write;
	    pthread_mutex_init(&(remaining_frames_mutex[i]), NULL);
	}

//	for (int i = 0; i < NCARDS; i++) {
//            ret = pthread_join(setup[i], NULL);
//        }

	pthread_t writer[writer_settings.nthreads];
	writer_thread_arg_t writer_thread_arg[writer_settings.nthreads];

	if (experiment_settings.nframes_to_write > 0) {
		auto start = std::chrono::system_clock::now();

		for (int i = 0; i < writer_settings.nthreads; i++) {
			writer_thread_arg[i].thread_id = i / NCARDS;
                        if (NCARDS > 1) 
			    writer_thread_arg[i].card_id   = i % NCARDS;
                        else
                            writer_thread_arg[i].card_id = 0;
			ret = pthread_create(&(writer[i]), NULL, writer_thread, &(writer_thread_arg[i]));
		}
                std::cout << "Threads started" << std::endl;
		for (int i = 0; i < writer_settings.nthreads; i++) {
			ret = pthread_join(writer[i], NULL);
		}

		auto end = std::chrono::system_clock::now();

		std::chrono::duration<double> diff = end - start;

		std::cout << "Throughput: " << (float) (experiment_settings.nframes_to_write) / diff.count() << " frames/s" << std::endl;
		std::cout << "Compression ratio: " << ((double) total_compressed_size * 8) / ((double) NPIXEL * (double) NCARDS * (double) experiment_settings.nframes_to_write) << " bits/pixel" <<std::endl;
	}

	for (int i = 0; i < NCARDS; i++)
			close_connection_card(i);

	// Only save pedestal and gain, if filename provided
	if (writer_settings.HDF5_prefix != "")
		save_gain_pedestal_hdf5();
}
