#include "hw_action_rx100G.h"

inline ap_uint<48> get_mac_addr(ap_uint<512> data, size_t position) {
	ap_uint<48> tmp = data(position+47,position);
	ap_uint<48> retval;
	// Swap endian
	for (int i = 0; i < 6; i++) {
#pragma HLS UNROLL
		retval(8*i+7,8*i) = tmp((5-i)*8+7, (5-i)*8);
	}
	return retval;
}

inline ap_uint<16> get_header_field_16(ap_uint<512> data, size_t position) {
	ap_uint<16> tmp = data(position+15, position);
	ap_uint<16> retval;
	// Swap endian
	retval(15,8) = tmp(7,0);
	retval(7,0) = tmp(15,8);
	return retval;
}

inline ap_uint<32> get_header_field_32(ap_uint<512> data, size_t position) {
	ap_uint<32> tmp = data(position+31, position);
	ap_uint<32> retval;
	// Swap endian
	retval(7,0) = tmp(31,24);
	retval(15,8) = tmp(23,16);
	retval(23,16) = tmp(15,8);
	retval(31,24) = tmp(7,0);
	return retval;
}


void decode_eth_1(ap_uint<512> val_in, packet_header_t &header_out) {
	header_out.dest_mac = get_mac_addr(val_in,0);
	header_out.src_mac  = get_mac_addr(val_in,48);
	header_out.ether_type = get_header_field_16(val_in, 12*8);
	ap_uint<32> eth_payload_pos = 14*8; // 112 bits

	header_out.ip_version = val_in(eth_payload_pos+8-1, eth_payload_pos+4);
	header_out.ipv4_header_len = val_in(eth_payload_pos+4-1,eth_payload_pos); // need to swap the two, don't know why??

	header_out.ipv4_protocol  = val_in(eth_payload_pos+80-1,eth_payload_pos+72);
	header_out.ipv4_total_len = get_header_field_16(val_in, eth_payload_pos+16);
	header_out.ipv4_source_ip = get_header_field_32(val_in, eth_payload_pos+96);
	header_out.ipv4_dest_ip = get_header_field_32(val_in, eth_payload_pos+128);
	ap_uint<32> ipv4_payload_pos = eth_payload_pos + 160; // 112 + 160 = 272 bits

	header_out.udp_src_port = get_header_field_16(val_in, ipv4_payload_pos);
	header_out.udp_dest_port = get_header_field_16(val_in, ipv4_payload_pos + 16);
	header_out.udp_len = get_header_field_16(val_in, ipv4_payload_pos + 32);

	ap_uint<32> udp_payload_pos = ipv4_payload_pos + 64; // usually 112 + 160 + 64 = 336 bits (42 bytes)
	ap_uint<32> jf_header_pos = udp_payload_pos + 6*8; // 384 bits / 48 bytes
	header_out.jf_frame_number = val_in(jf_header_pos + 64-1, jf_header_pos);
	header_out.jf_exptime = val_in(jf_header_pos + 96-1, jf_header_pos + 64);
	header_out.jf_packet_number = val_in(jf_header_pos + 128 - 1, jf_header_pos + 96);
}
