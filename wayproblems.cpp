#include <cstdlib>  // for std::exit
#include <cstring>  // for std::strcmp
#include <iostream> // for std::cout, std::cerr

#include <osmium/geom/ogr.hpp>
#include <osmium/osm/way.hpp>

// For the NodeLocationForWays handler
#include <osmium/handler/node_locations_for_ways.hpp>

// Allow any format of input files (XML, PBF, ...)
#include <osmium/io/any_input.hpp>

// For osmium::apply()
#include <osmium/visitor.hpp>

// For the location index. There are different types of indexes available.
// This will work for all input files keeping the index in memory.
#include <osmium/index/map/flex_mem.hpp>

#include <gdalcpp.hpp>
#include <boost/program_options.hpp>

#include "yamlrules.hpp"

// The type of index used. This must match the include file above
using index_type = osmium::index::map::FlexMem<osmium::unsigned_object_id_type, osmium::Location>;

// The location handler always depends on the index type
using location_handler_type = osmium::handler::NodeLocationsForWays<index_type>;

#define DEBUG 0

enum layerid {
	L_WP,
	L_REF,
	L_FOOTWAY,
	L_DEFAULTS,
	L_STRANGE,
	L_CYCLING,
	L_MAXSPEED,
	L_ZONETRAFFIC,
	layermax
};

class SpatiaLiteWriter : public osmium::handler::Handler {
	std::array<gdalcpp::Layer *, layermax>	layer;
	std::array<std::string, layermax>	layername;
	std::map<std::string, layerid>		layerids;

	gdalcpp::Dataset		dataset;
	osmium::geom::OGRFactory<>	m_factory{};

	public:

	explicit SpatiaLiteWriter(std::string &dbname) :
			dataset("sqlite", dbname, gdalcpp::SRS{}, {  "SPATIALITE=TRUE", "INIT_WITH_EPSG=no" }) {

			dataset.exec("PRAGMA synchronous = OFF");

			addLineStringLayer(L_WP, "wayproblems");
			addLineStringLayer(L_REF, "ref");
			addLineStringLayer(L_FOOTWAY, "footway");
			addLineStringLayer(L_STRANGE, "strange");
			addLineStringLayer(L_CYCLING, "cycling");
			addLineStringLayer(L_MAXSPEED, "maxspeed");
			addLineStringLayer(L_ZONETRAFFIC, "zonetraffic");
			addLineStringLayer(L_DEFAULTS, "defaults");
		}

	void addLineStringLayer(const int id, const char *name) {
		gdalcpp::Layer *l=new gdalcpp::Layer(dataset, name, wkbLineString);

		l->add_field("id", OFTString, 20);
		l->add_field("key", OFTString, 20);
		l->add_field("value", OFTString, 20);
		l->add_field("changeset", OFTString, 20);
		l->add_field("user", OFTString, 20);
		l->add_field("timestamp", OFTString, 20);
		l->add_field("problem", OFTString, 60);
		l->add_field("version", OFTString, 60);
		l->add_field("style", OFTString, 20);

		layername[id]=name;
		layer[id]=l;
		layerids[name]=static_cast<enum layerid>(id);
	}

	layerid layer_id(const std::string &name) const {
		auto found=layerids.find(name);
		if (found == layerids.end())
			throw std::runtime_error("unknown layer '" + name + "'");
		return found->second;
	}

	std::vector<std::string> layer_names() const {
		std::vector<std::string> names;
		for (const auto &name : layername)
			names.push_back(name);
		return names;
	}

	void writeWay(layerid lid, const osmium::Way& way, const char *style, const char *format, ...) {
		try  {
			std::unique_ptr<OGRLineString>	linestring = m_factory.create_linestring(way);
			char				problem[256];
			va_list				args;

			va_start(args, format);
			vsnprintf (problem, 255, format, args);
			va_end (args);

			gdalcpp::Feature feature{*layer[lid], std::move(linestring)};

			feature.set_field("id", static_cast<double>(way.id()));
			feature.set_field("user", way.user());
			feature.set_field("changeset", static_cast<double>(way.changeset()));
			feature.set_field("timestamp", way.timestamp().to_iso().c_str());
			feature.set_field("problem", problem);
			feature.set_field("style", style);
			feature.set_field("version", static_cast<double>(way.version()));

			feature.add_to_layer();

			std::cout << "way=" << way.id() << " problem=\"" << problem << "\" || "
				<< " changeset=" << way.changeset()
				<< " user=\"" << way.user() << "\""
				<< " timestamp=" << way.timestamp().to_iso()
				<< " layer=" << layername[lid]
				<< " version=" << way.version()
				<< std::endl;

		} catch (const gdalcpp::gdal_error& e) {
			std::cerr << "gdal_error while creating feature wayid " << way.id()<< std::endl;
		} catch (const osmium::invalid_location& e) {
			std::cerr << "invalid location wayid " << way.id() << std::endl;
		} catch (const osmium::geometry_error& e) {
			std::cerr << "geometry error wayid " << way.id() << std::endl;
		}
	}
};

class extendedTagList : public TagLookup {
	const osmium::TagList&	taglist;
	const bool		closed;

	public:
		extendedTagList(const osmium::TagList& tags, bool is_closed) :
				taglist(tags),
				closed(is_closed) { }

		bool has_key(const char *key) const override {
			return taglist.has_key(key);
		}

		const char *get_value_by_key(const char *key) const override {
			return taglist.get_value_by_key(key);
		}

		bool has_fact(const char *fact) const override {
			return 0 == strcmp(fact, "closed") && closed;
		}
};


class WayHandler : public osmium::handler::Handler, public RuleEngine::Sink {
	SpatiaLiteWriter	&writer;
	RuleEngine		*rules;
	osmium::Way		*current_way;

	public:
		WayHandler(SpatiaLiteWriter &writer, RuleEngine *rules) : writer(writer), rules(rules), current_way(nullptr) {};

		void report(const std::string &layer, const std::string &style, const std::string &problem) override {
			writer.writeWay(writer.layer_id(layer), *current_way, style.c_str(), "%s", problem.c_str());
		}

		void way(osmium::Way& way) {
			if (!rules)
				return;

			current_way=&way;

			extendedTagList taglist(way.tags(), way.ends_have_same_id());
			rules->evaluate(*this, taglist);
		}
};


namespace po = boost::program_options;

int main(int argc, char* argv[]) {

	po::options_description         desc("Allowed options");
        desc.add_options()
                ("help,h", "produce help message")
                ("infile,i", po::value<std::string>()->required(), "Input file")
		("dbname,d", po::value<std::string>()->required(), "Output database name")
		("rules,r", po::value<std::string>()->default_value("rules.yaml"), "YAML ruleset file")
        ;
        po::variables_map vm;
	try {
		po::store(po::parse_command_line(argc, argv, desc), vm);
		po::notify(vm);
	} catch(boost::program_options::required_option& e) {
		std::cerr << "Error: " << e.what() << "\n";
		exit(-1);
	}

	RuleEngine	rules;

	try {
		rules.load(vm["rules"].as<std::string>());
	} catch(const std::exception& e) {
		std::cerr << "Error in ruleset " << vm["rules"].as<std::string>() << ": " << e.what() << "\n";
		exit(-1);
	}

	osmium::io::File input_file{vm["infile"].as<std::string>()};

	// The index storing all node locations.
	index_type index;

	// The handler that stores all node locations in the index and adds them
	// to the ways.
	location_handler_type location_handler{index};

	// If a location is not available in the index, we ignore it. It might
	// not be needed (if it is not part of a multipolygon relation), so why
	// create an error?
	location_handler.ignore_errors();

	OGRRegisterAll();
	std::string		dbname=vm["dbname"].as<std::string>();
	SpatiaLiteWriter	writer{dbname};

	try {
		rules.set_known_layers(writer.layer_names());
	} catch(const std::exception& e) {
		std::cerr << "Error in ruleset " << vm["rules"].as<std::string>() << ": " << e.what() << "\n";
		exit(-1);
	}

	WayHandler	handler(writer, &rules);

	osmium::io::Reader reader{input_file};
	osmium::apply(reader, location_handler, handler);
	reader.close();
}
