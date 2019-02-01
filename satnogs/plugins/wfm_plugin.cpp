#include "plugin.hpp"
#include <iostream>
#include <boost/config.hpp>
#include <gnuradio/hier_block2.h>
#include <gnuradio/io_signature.h>
#include <gnuradio/analog/quadrature_demod_cf.h>
#include <gnuradio/attributes.h>
#include <gnuradio/filter/freq_xlating_fir_filter_ccf.h>
#include <gnuradio/filter/rational_resampler_base_fff.h>
#include <gnuradio/filter/firdes.h>
#include <gnuradio/filter/fir_filter_fff.h>
#include <gnuradio/analog/quadrature_demod_cf.h>

#ifdef gnuradio_wfm_hier_block_EXPORTS
#  define HIER_BLOCK_TEST_API __GR_ATTR_EXPORT
#else
#  define HIER_BLOCK_TEST_API __GR_ATTR_IMPORT
#endif

namespace wfm_plugin {

	int greatest_common_divisor(int a,int b) {
		int x;
		while (b > 0) {
			x = a % b;
			a = b;
			b = x;
		}
		return a;
	}

	class HIER_BLOCK_TEST_API wfm_hier_block : virtual public gr::hier_block2
	{
	 public:
		typedef boost::shared_ptr<wfm_hier_block> sptr;

		/*!
		 * \brief Return a shared_ptr to a new instance of hier_block_test::hier_test.
		 *
		 * To avoid accidental use of raw pointers, hier_block_test::hier_test's
		 * constructor is in a private implementation
		 * class. hier_block_test::hier_test::make is the public interface for
		 * creating new instances.
		 */
		static sptr make();
	};


	class wfm_hier_block_impl : public wfm_hier_block
	{
	 private:
		// Nothing to declare in this block.

	 public:
		wfm_hier_block_impl();
		~wfm_hier_block_impl();


		// Where all the action really happens
	};

	wfm_hier_block::sptr wfm_hier_block::make() {
		return gnuradio::get_initial_sptr
			(new wfm_hier_block_impl());
	}

	/*
	 * The private constructor
	 */
	wfm_hier_block_impl::wfm_hier_block_impl()
	  : gr::hier_block2("wfm_hier_block",
		  gr::io_signature::make(1, 1, sizeof(gr_complex)),
		  gr::io_signature::make(1, 1, sizeof(float)))
	{
		int audio_sampling_rate = 48'000;
		double rx_sampling_rate = 2'000'000;
		double lo_offset = 250'000;
		double filter_rate = 500'000;
		int fir_decimation = rx_sampling_rate/filter_rate;
		int audio_decimate = 10;

		int gcd = greatest_common_divisor(audio_sampling_rate,filter_rate/audio_decimate);
		int interpolation = audio_sampling_rate / gcd;
		int decimation = (filter_rate/audio_decimate) / gcd;
		double rate = (float) interpolation / (float) decimation;
		double fractional_bw = 0.4;
		double beta = 7.0;
		double halfband = 0.5;

		double trans_width = rate*(halfband - fractional_bw);
		double mid_transition_band = rate*halfband - trans_width/2.0f;

		std::vector<float> rr_taps = gr::filter::firdes::low_pass(interpolation,
		                                                          interpolation,
		                                                          mid_transition_band,
		                                                          trans_width,
		                                                          gr::filter::firdes::WIN_KAISER,
		                                                          beta);

		std::vector<float> taps = gr::filter::firdes::low_pass(1, rx_sampling_rate, 100'000, 100'000);
		double width_of_transition_band = audio_sampling_rate/32;
		std::vector<float> audio_taps = gr::filter::firdes::low_pass(1.0, filter_rate,
		                                                             audio_sampling_rate/2 - width_of_transition_band,
		                                                             width_of_transition_band);

		gr::filter::freq_xlating_fir_filter_ccf::sptr fir_filter(gr::filter::freq_xlating_fir_filter_ccf::make(fir_decimation, taps, lo_offset, rx_sampling_rate));
		gr::filter::rational_resampler_base_fff::sptr resampler(gr::filter::rational_resampler_base_fff::make(interpolation, decimation, rr_taps));

		const float PI = 3.145926;
		float max_dev = 75e3;

		gr::analog::quadrature_demod_cf::sptr quad_mod(gr::analog::quadrature_demod_cf::make(filter_rate/(2*PI*max_dev)));

		gr::filter::fir_filter_fff::sptr audio_filter(gr::filter::fir_filter_fff::make(audio_decimate, audio_taps));

		connect(self(), 0, fir_filter, 0);
		connect(fir_filter, 0, quad_mod, 0);
		connect(quad_mod, 0, audio_filter, 0);
		connect(audio_filter, 0, resampler, 0);
		connect(resampler, 0, self(), 0);
	}

	/*
	 * Our virtual destructor.
	 */
	 wfm_hier_block_impl::~wfm_hier_block_impl()
	{
	}


	class satnogs_wfm_demod : public satnogs_plugin_block {
		private:
			gr::hier_block2_sptr wfm_decoder;
		public:
			satnogs_wfm_demod() { 
				std::cout << "Constructing wfm plugin" << std::endl;
				this->wfm_decoder = wfm_hier_block::make();
			}

			std::string name() const {
				return "WFM";
			}

			satnogs_plugin_type get_plugin_type() {
				return satnogs_plugin_type::SATNOGS_DEMODER;
			}

			~satnogs_wfm_demod() {
				std::cout << "Desctructing wfm plugin" << std::endl;
			}

			gr::hier_block2_sptr block() {
				return this->wfm_decoder;
			}
	};

	extern "C" BOOST_SYMBOL_EXPORT satnogs_wfm_demod plugin;
	satnogs_wfm_demod plugin;

}
