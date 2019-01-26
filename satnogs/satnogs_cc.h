#include <gnuradio/top_block.h>
#include <gnuradio/analog/noise_source_c.h>
#include <gnuradio/blocks/udp_sink.h>
#include <boost/dll/import.hpp>
#include "plugins/plugin.hpp"

using namespace gr;

class top_block_ {
	private:
		hier_block2_sptr demoder_block;
		hier_block2_sptr decoder_block;
		std::map<std::string, boost::shared_ptr<satnogs_plugin_block>> plugin_registry;
	// Variables:
		int samp_rate = 32000;
	public:
		top_block_sptr tb;
		top_block_();
		~top_block_();

		int get_samp_rate () const;
		void set_samp_rate(int samp_rate);
		void set_mode(std::string mode);
};

