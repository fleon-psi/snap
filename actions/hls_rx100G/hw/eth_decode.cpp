#include "hw_action_rx100G.h"

enum rcv_state_t {RCV_INIT, RCV_JF_HEADER, RCV_GOOD, RCV_BAD, RCV_IGNORE};

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
	// IP/UDP header is big endian, JF header is small endian!

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

	ap_uint<32> udp_payload_pos = ipv4_payload_pos + 64; // 112 + 160 + 64 = 336 bits (42 bytes)

	header_out.jf_frame_number = val_in(ipv4_payload_pos + 64-1, ipv4_payload_pos);
	header_out.jf_exptime = val_in(ipv4_payload_pos + 96-1, ipv4_payload_pos + 64);
	header_out.jf_packet_number = val_in(ipv4_payload_pos + 128 - 1, ipv4_payload_pos + 96);
	header_out.jf_bunch_id(63,16) = val_in(ipv4_payload_pos + 176 - 1, ipv4_payload_pos + 128);
}

void decode_eth_2(ap_uint<512> val_in, packet_header_t &header_out) {
	// bunch ID is recorded between two AXIS packet
	header_out.jf_bunch_id(15,0) = val_in(16-1,0);
	header_out.jf_timestamp = val_in(80-1,16);
	header_out.jf_module_id = val_in(80+16-1,80);
	header_out.jf_xcoord = val_in(80+32-1,80+16);
	header_out.jf_ycoord = val_in(80+48-1,80+32);
	header_out.jf_zcoord = val_in(80+64-1,80+48);
	header_out.jf_debug = val_in(144+32-1, 144);
	header_out.jf_round_robin = val_in(144+48-1,144+32);
	header_out.jf_detector_type = val_in(144+56-1,144+48);
	header_out.jf_header_version_type = val_in(208-1,200);
}

void read_eth_packet(AXI_STREAM &in, DATA_STREAM &out, eth_settings_t eth_settings, eth_stat_t &eth_stat) {

	rcv_state_t rcv_state = RCV_INIT;
	uint64_t packets_read = 0;
	ap_uint<8> axis_packet = 0; // 0..256 , but >=128 means error
	ap_axiu_for_eth packet_in;
	data_packet_t packet_out;
	packet_header_t header;
	packet_out.exit = 0;

	while (packets_read < eth_settings.expected_packets) {
#pragma HLS PIPELINE
		in.read(packet_in);
		switch (rcv_state) {
		case RCV_INIT:
			decode_eth_1(packet_in.data, header);
			if ((header.dest_mac == eth_settings.fpga_mac_addr) &&
					(header.ether_type == 0x0800) && // IP
					(header.ip_version == 4) && // IPv4
					(header.ipv4_protocol == 0x11) && // UDP
					(header.ipv4_total_len == 8268)) {
				rcv_state = RCV_JF_HEADER;
				axis_packet = 0;
			}
			else rcv_state = RCV_IGNORE;
			break;
		case RCV_JF_HEADER:
			rcv_state = RCV_GOOD;
			decode_eth_2(packet_in.data, header);
			packet_out.frame_number = header.jf_frame_number;
			packet_out.eth_packet = header.jf_packet_number;
			packet_out.module = header.src_mac % NMODULES;
			packet_out.trigger = header.jf_debug[31];
			packet_out.data(303,0) = packet_in.data(511, 208);
			break;
		case RCV_GOOD:
			if (axis_packet == 128) rcv_state = RCV_BAD;
			else {
				packet_out.axis_packet = axis_packet;
				packet_out.data(511,304) = packet_in.data(207, 0);
				out.write(packet_out);
				packet_out.data(303,0) = packet_in.data(511, 208);
				axis_packet++;
			}

			break;
		case RCV_BAD:
		case RCV_IGNORE:
			break;
		}
		if (packet_in.last == 1) {
			if ((rcv_state == RCV_BAD) || ((rcv_state != RCV_IGNORE) && (axis_packet != 128)) || (packet_in.user == 1)) eth_stat.bad_packets++;
			else if (rcv_state == RCV_GOOD) eth_stat.good_packets++;
			else if (rcv_state == RCV_IGNORE) eth_stat.ignored_packets++;
		}
	}
	packet_out.exit = 1;
	out.write(packet_out); // Inform writer, that all is finished
}
