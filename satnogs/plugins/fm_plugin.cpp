#include "plugin.hpp"
#include <iostream>
#include <boost/config.hpp>
#include <gnuradio/hier_block2.h>
#include <gnuradio/io_signature.h>
#include <gnuradio/analog/quadrature_demod_cf.h>
#include <gnuradio/attributes.h>

#ifdef gnuradio_fm_hier_block_EXPORTS
#  define HIER_BLOCK_TEST_API __GR_ATTR_EXPORT
#else
#  define HIER_BLOCK_TEST_API __GR_ATTR_IMPORT
#endif

namespace fm_plugin {

	class HIER_BLOCK_TEST_API fm_hier_block : virtual public gr::hier_block2
	{
	 public:
		typedef boost::shared_ptr<fm_hier_block> sptr;

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


	class fm_hier_block_impl : public fm_hier_block
	{
	 private:
		// Nothing to declare in this block.

	 public:
		fm_hier_block_impl();
		~fm_hier_block_impl();


		// Where all the action really happens
	};

	fm_hier_block::sptr fm_hier_block::make() {
		return gnuradio::get_initial_sptr
			(new fm_hier_block_impl());
	}

	/*
	 * The private constructor
	 */
	fm_hier_block_impl::fm_hier_block_impl()
	  : gr::hier_block2("fm_hier_block",
		  gr::io_signature::make(1, 1, sizeof(gr_complex)),
		  gr::io_signature::make(1, 1, sizeof(float)))
	{
		gr::analog::quadrature_demod_cf::sptr quad_demod(gr::analog::quadrature_demod_cf::make(1.0f));
		connect(self(), 0, quad_demod, 0);
		// connect other blocks
		connect(quad_demod, 0, self(), 0);
	}

	/*
	 * Our virtual destructor.
	 */
	 fm_hier_block_impl::~fm_hier_block_impl()
	{
	}


	class satnogs_fm_demod : public satnogs_plugin_block {
		private:
			gr::hier_block2_sptr fm_decoder;
		public:
			satnogs_fm_demod() { 
				std::cout << "Constructing fm plugin" << std::endl;
				this->fm_decoder = fm_hier_block::make();
			}

			std::string name() const {
				return "FM";
			}

			satnogs_plugin_type get_plugin_type() {
				return satnogs_plugin_type::SATNOGS_DEMODER;
			}

			~satnogs_fm_demod() {
				std::cout << "Desctructing fm plugin" << std::endl;
			}

			gr::hier_block2_sptr block() {
				return this->fm_decoder;
			}
	};

	extern "C" BOOST_SYMBOL_EXPORT satnogs_fm_demod plugin;
	satnogs_fm_demod plugin;

}
