#include "satnogs_cc.h"

using namespace gr;

top_block_::top_block_ () {
		this->tb = gr::make_top_block("top_block");
	// Blocks:
		{
			this->blocks_udp_sink_0 = blocks::udp_sink::make(sizeof(gr_complex), "127.0.0.1", 1337, 1472, true);
		}
		{
			this->analog_noise_source_x_0 = analog::noise_source_c::make(analog::GR_GAUSSIAN, 1, 0);
		}
	// Connections:
		this->tb->hier_block2::connect(this->analog_noise_source_x_0, 0, this->blocks_udp_sink_0, 0);
	}
	top_block_::~top_block_ () {
	}
	// Callbacks:
	int top_block_::get_samp_rate () const {
		return this->samp_rate;
	}
	void top_block_::set_samp_rate (int samp_rate) {
		this->samp_rate = samp_rate;
}

// C++ code:

extern "C" void start_gnuradio();
extern "C" void stop_gnuradio();
top_block_* my_top_block;

void start_gnuradio()
{
	my_top_block = new top_block_();
	my_top_block->tb->start();
}

void stop_gnuradio()
{
	my_top_block->tb->stop();
	my_top_block->tb->wait();
}
