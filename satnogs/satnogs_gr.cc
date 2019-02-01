#include "satnogs_cc.h"
#include <osmosdr/source.h>
#include <gnuradio/audio/sink.h>

using namespace gr;
namespace dll = boost::dll;


top_block_::top_block_ () {
	this->tb = gr::make_top_block("top_block");
	// todo make this configurable
	std::string path = "/home/hpfmn/GIT/satNOGS/Hamlib/satnogs/plugins";
	boost::filesystem::path lib_path(path);
	boost::shared_ptr<satnogs_plugin_block> plugin;

	for (auto& entry: boost::filesystem::directory_iterator(lib_path)) {
			if (entry.path().extension() == ".so" || entry.path().extension() == ".dll") {
				std::cout << "Loading the plugin" << std::endl;
				plugin = dll::import<satnogs_plugin_block>(entry, "plugin");
				plugin_registry[plugin->name()] = plugin;
				switch(plugin->get_plugin_type()) {
					case satnogs_plugin_type::SATNOGS_DECODER:
						std::cout << "DECODER type" << std::endl;
						break;
					case satnogs_plugin_type::SATNOGS_DEMODER:
						std::cout << "DEMODER type" << std::endl;
						break;
				}
			}
	}

	this->set_mode("WFM");

	int audio_sampling_rate = 48'000;
	double rx_sampling_rate = 2'000'000;
	double lo_offset = 250'000;
	double f_rx = 99'300'000;

	osmosdr::source::sptr sdr(osmosdr::source::make("driver=lime,soapy=0"));
	sdr->set_sample_rate(rx_sampling_rate);
	sdr->set_center_freq(f_rx - lo_offset);

	gr::audio::sink::sptr audio_out(gr::audio::sink::make(audio_sampling_rate));

	this->tb->hier_block2::connect(sdr, 0, this->demoder_block, 0);
	this->tb->hier_block2::connect(this->demoder_block, 0, audio_out, 0);
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

void top_block_::set_mode (std::string mode) {
	std::cout << "LOAD demoder Block from plugin: " << this->plugin_registry[mode]->name();
	this->demoder_block = this->plugin_registry[mode]->block();
	// probably do connection etc. here
}

// C++ code:

extern "C" void start_gnuradio();
extern "C" void stop_gnuradio();
extern "C" void satnogs_gr_set_mode(const char* mode);
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

void satnogs_gr_set_mode(const char* mode) {
	my_top_block->set_mode(mode);
}
