#include "hw_action_rx100G.h"

void convert(DATA_STREAM &raw_in, DATA_STREAM &converted_out) {
	data_packet_t packet_in;
	data_packet_t packet_out;
	raw_in.read(packet_in);
	while (packet_in.exit == 0) {
		raw_in.read(packet_in);
		packet_out = packet_in;
		converted_out.write(packet_out);
	}
}
