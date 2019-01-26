#include <gnuradio/hier_block2.h>
#include <string>

enum class satnogs_plugin_type { SATNOGS_DEMODER, SATNOGS_DECODER };

class satnogs_plugin_block {
	public:
		virtual std::string name() const = 0;
		virtual satnogs_plugin_type get_plugin_type() = 0;
		virtual ~satnogs_plugin_block() {}
		virtual gr::hier_block2_sptr block() = 0;
};
