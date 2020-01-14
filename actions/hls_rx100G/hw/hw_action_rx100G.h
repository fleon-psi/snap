#ifndef __HW_ACTION_RX100G_H__
#define __HW_ACTION_RX100G_H__

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

#include <stdint.h>
#include <string.h>
#include <ap_int.h>
#include <hls_stream.h>

#include "hls_snap.H"
#include "../include/action_rx100G.h" /* HelloWorld Job definition */

//--------------------------------------------------------------------
// 1: simplify the data casting style
#define RELEASE_LEVEL		0x00000002

typedef char word_t[BPERDW];
//---------------------------------------------------------------------
// This is generic. Just adapt names for a new action
// CONTROL is defined and handled by SNAP 
// helloworld_job_t is user defined in hls_helloworld/include/action_change_case.h
typedef struct {
	CONTROL Control;	/*  16 bytes */
	rx100G_job_t Data;	/* up to 108 bytes */
	uint8_t padding[SNAP_HLS_JOBSIZE - sizeof(rx100G_job_t)];
} action_reg;

// Based on https://forums.xilinx.com/t5/High-Level-Synthesis-HLS/ap-axiu-parameters/td-p/635138
  struct ap_axiu_for_eth {
    ap_uint<512>     data;
    ap_uint<64>      keep;
    ap_uint<1>       user;
    ap_uint<1>       last;
  };

  struct packet_header_t {
  	ap_uint<48> dest_mac ;
  	ap_uint<48> src_mac  ;
  	ap_uint<16> ether_type ;

  	ap_uint<4> ip_version ;
  	ap_uint<4> ipv4_header_len ;
  	ap_uint<8> ipv4_protocol  ;
  	ap_uint<32> ipv4_total_len ;
  	// uint32_t ipv4_header_checksum;
  	ap_uint<32> ipv4_source_ip ;
  	ap_uint<32> ipv4_dest_ip ;

  	ap_uint<16> udp_src_port;
  	ap_uint<16> udp_dest_port;
  	ap_uint<16> udp_len;
  	ap_uint<16> udp_checksum;

  	ap_uint<64> jf_frame_number;
  	ap_uint<32> jf_exptime;
  	ap_uint<32> jf_packet_number;
  //	ap_uint<64> jf_bunch_id;
  //	ap_uint<64> jf_timestamp;
  // 	ap_uint<16> jf_module_id;
  //	ap_uint<16> jf_xcoord;
  //	ap_uint<16> jf_ycoord;
  //	ap_uint<16> jf_zcoord;
  //	ap_uint<32> jf_debug;
  //	ap_uint<16> jf_round_robin;
  //	ap_uint<8> jf_detector_type;
  //	ap_uint<8> jf_header_version_type;
  };

  void decode_eth_1(ap_uint<512> val_in, packet_header_t &header_out) ;

typedef hls::stream<ap_axiu_for_eth> AXI_STREAM;

#endif  /* __ACTION_RX100G_H__*/