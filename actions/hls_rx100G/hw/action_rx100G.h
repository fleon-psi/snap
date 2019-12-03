#ifndef __ACTION_UPPERCASE_H__
#define __ACTION_UPPERCASE_H__

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
#include <action_changecase.h> /* HelloWorld Job definition */

//--------------------------------------------------------------------
// 1: simplify the data casting style
#define RELEASE_LEVEL		0x00000001

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

typedef hls::stream<ap_axiu_for_eth> AXI_STREAM;

#endif  /* __ACTION_UPPERCASE_H__*/
