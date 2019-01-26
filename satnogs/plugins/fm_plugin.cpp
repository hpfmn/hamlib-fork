#include "plugin.hpp"
#include <iostream>
#include <boost/config.hpp>

namespace fm_plugin {

	class satnogs_fm_demod : public satnogs_plugin_block {
		private:
			gr::hier_block2_sptr fm_decoder;
		public:
			satnogs_fm_demod() { 
				std::cout << "Constructing fm plugin" << std::endl;
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
