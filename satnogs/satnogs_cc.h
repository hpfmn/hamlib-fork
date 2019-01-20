#include <gnuradio/top_block.h>
#include <gnuradio/analog/noise_source_c.h>
#include <gnuradio/blocks/udp_sink.h>

using namespace gr;

class top_block_ {
	private:
		blocks::udp_sink::sptr blocks_udp_sink_0;
		analog::noise_source_c::sptr analog_noise_source_x_0;
	// Variables:
		int samp_rate = 32000;
	public:
		top_block_sptr tb;
		top_block_();
		~top_block_();

		int get_samp_rate () const;
		void set_samp_rate(int samp_rate);
};

