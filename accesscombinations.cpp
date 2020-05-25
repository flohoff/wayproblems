#include <cstdlib>  // for std::exit
#include <cstring>  // for std::strcmp
#include <iostream> // for std::cout, std::cerr

// For the DynamicHandler class
#include <osmium/dynamic_handler.hpp>

// For the WKT factory
#include <osmium/geom/wkt.hpp>
#include <osmium/geom/ogr.hpp>
#include <osmium/osm/way.hpp>

// For the Dump handler
#include <osmium/handler/dump.hpp>

// Allow any format of input files (XML, PBF, ...)
#include <osmium/io/any_input.hpp>

// For osmium::apply()
#include <osmium/visitor.hpp>

#include <boost/program_options.hpp>

#define DEBUG 0

class WayHandler : public osmium::handler::Handler {
	public:
		void way(osmium::Way& way) {
			const osmium::TagList& taglist=way.tags();

			if (!taglist.has_key("highway")) {
				return;
			}

			std::vector<const char *>	dumptags={
				"highway", "access", "vehicle", "motor_vehicle", "motorcycle",
				"motorcar", "hgv", "psv", "bicycle", "foot", "agricultural",
				"goods", "mofa", "moped", "horse"};

				for(auto key : dumptags) {
					const char *value=taglist.get_value_by_key(key);
					if (!value)
						continue;
					std::cout << key << "=" << value << " ";
				}
				std::cout << std::endl;
			}
};


namespace po = boost::program_options;

int main(int argc, char* argv[]) {

	po::options_description         desc("Allowed options");
        desc.add_options()
                ("help,h", "produce help message")
                ("infile,i", po::value<std::string>()->required(), "Input file")
        ;
        po::variables_map vm;

	try {
		po::store(po::parse_command_line(argc, argv, desc), vm);
		po::notify(vm);
	} catch(boost::program_options::required_option& e) {
		std::cout << "Error: " << e.what() << "\n";
                std::cout << desc << "\n";
		exit(-1);
	}

        if (vm.count("help")) {
                std::cout << desc << "\n";
                return 1;
        }

	// Initialize an empty DynamicHandler. Later it will be associated
	// with one of the handlers. You can think of the DynamicHandler as
	// a kind of "variant handler" or a "pointer handler" pointing to the
	// real handler.
	osmium::io::File input_file{vm["infile"].as<std::string>()};

	WayHandler	handler;

	osmium::io::Reader reader{input_file};
	osmium::apply(reader, handler);
	reader.close();
}

