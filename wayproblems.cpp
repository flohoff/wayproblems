#include <cctype>   // for std::tolower
#include <cstdlib>  // for std::exit
#include <cstring>  // for std::strcmp
#include <fstream>  // for std::ofstream
#include <iostream> // for std::cout, std::cerr
#include <memory>   // for std::unique_ptr, std::make_unique

#include <nlohmann/json.hpp>

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

// All writers write the same 8 layers.
class ProblemWriter {
	protected:
		bool verbose;
		ProblemWriter(bool is_verbose) : verbose(is_verbose) { }

	public:
		virtual void writeWay(const std::string &layer, const osmium::Way& way, const char *style,
				const std::string &problem) = 0;
		virtual std::vector<std::string> layer_names() const = 0;
		virtual ~ProblemWriter() = default;
};

class SpatiaLiteWriter : public ProblemWriter {
	std::array<gdalcpp::Layer *, layermax>	layer;
	std::array<std::string, layermax>	layername;
	std::map<std::string, layerid>		layerids;

	gdalcpp::Dataset		dataset;
	osmium::geom::OGRFactory<>	m_factory{};

	public:

	explicit SpatiaLiteWriter(const std::string &outfile, bool is_verbose=false) :
			ProblemWriter(is_verbose),
			dataset("sqlite", outfile, gdalcpp::SRS{}, {  "SPATIALITE=TRUE", "INIT_WITH_EPSG=no" }) {

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

	void writeWay(const std::string &layer, const osmium::Way& way, const char *style,
			const std::string &problem) override {
		layerid lid=layer_id(layer);
		try  {
			std::unique_ptr<OGRLineString>	linestring = m_factory.create_linestring(way);

			gdalcpp::Feature feature{*this->layer[lid], std::move(linestring)};

			feature.set_field("id", static_cast<double>(way.id()));
			feature.set_field("user", way.user());
			feature.set_field("changeset", static_cast<double>(way.changeset()));
			feature.set_field("timestamp", way.timestamp().to_iso().c_str());
			feature.set_field("problem", problem.c_str());
			feature.set_field("style", style);
			feature.set_field("version", static_cast<double>(way.version()));

			feature.add_to_layer();

			if (verbose) {
				std::cout << "way=" << way.id() << " problem=\"" << problem << "\" || "
					<< " changeset=" << way.changeset()
					<< " user=\"" << way.user() << "\""
					<< " timestamp=" << way.timestamp().to_iso()
					<< " layer=" << this->layername[lid]
					<< " version=" << way.version()
					<< std::endl;
			}

		} catch (const gdalcpp::gdal_error& e) {
			std::cerr << "gdal_error while creating feature wayid " << way.id()<< std::endl;
		} catch (const osmium::invalid_location& e) {
			std::cerr << "invalid location wayid " << way.id() << std::endl;
		} catch (const osmium::geometry_error& e) {
			std::cerr << "geometry error wayid " << way.id() << std::endl;
		}
	}
};

// GeoJSON FeatureCollection; features are streamed one per line without
// keeping more than one feature in memory: the dumped text of the previous
// feature is delayed so the separating comma can go behind it.
class GeoJSONWriter : public ProblemWriter {
	std::ofstream			out;
	std::unique_ptr<std::string>	pending;

	public:
		explicit GeoJSONWriter(const std::string &outfile, bool is_verbose=false) :
				ProblemWriter(is_verbose),
				out(outfile, std::ios::out | std::ios::trunc) {
			if (!out.is_open())
				throw std::runtime_error("cannot open output file '" + outfile + "'");
			out << "{\"type\": \"FeatureCollection\", \"features\": [\n";
		}

		~GeoJSONWriter() {
			if (pending)
				out << *pending << "\n]}\n";
			else
				out << "]}\n";
		}

		std::vector<std::string> layer_names() const override {
			return { "wayproblems", "ref", "footway", "strange", "cycling",
				"maxspeed", "zonetraffic", "defaults" };
		}

		void writeWay(const std::string &layer, const osmium::Way& way, const char *style,
				const std::string &problem) override {
			try {
				nlohmann::json coordinates;
				for (const auto &node_ref : way.nodes()) {
					if (!node_ref.location().valid())
						throw osmium::invalid_location("way " + std::to_string(way.id()));
					coordinates.push_back({ node_ref.location().lon(), node_ref.location().lat() });
				}

				nlohmann::json feature = {
					{ "type", "Feature" },
					{ "geometry", {
						{ "type", "LineString" },
						{ "coordinates", std::move(coordinates) }
					}},
					{ "properties", {
						{ "layer", layer },
						{ "id", way.id() },
						{ "user", way.user() },
						{ "changeset", way.changeset() },
						{ "timestamp", way.timestamp().to_iso() },
						{ "problem", problem },
						{ "version", static_cast<uint32_t>(way.version()) },
						{ "style", style }
					}}
				};

				std::string dumped=feature.dump();

				// delayed comma of the previous feature keeps the JSON valid
				if (pending)
					out << *pending << ",\n";
				else
					pending=std::make_unique<std::string>();
				*pending=std::move(dumped);

				if (verbose) {
					std::cout << "way=" << way.id() << " problem=\"" << problem << "\" || "
						<< " changeset=" << way.changeset()
						<< " user=\"" << way.user() << "\""
						<< " timestamp=" << way.timestamp().to_iso()
						<< " layer=" << layer
						<< " version=" << way.version()
						<< std::endl;
				}

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
	ProblemWriter		&writer;
	RuleEngine		*rules;
	osmium::Way		*current_way;

	public:
		WayHandler(ProblemWriter &writer, RuleEngine *rules) : writer(writer), rules(rules), current_way(nullptr) {};

		void report(const std::string &layer, const std::string &style, const std::string &problem) override {
			writer.writeWay(layer, *current_way, style.c_str(), problem);
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
		("outfile,o", po::value<std::string>()->required(),
			"Output file (newline-delimited GeoJSON for .json/.geojson, SpatiaLite database otherwise)")
		("rules,r", po::value<std::string>()->default_value("rules.yaml"), "YAML ruleset file")
		("verbose,v", "log every report to stdout")
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
	std::string		outfile=vm["outfile"].as<std::string>();

	// A case-insensitive .json/.geojson suffix selects the GeoJSON writer.
	std::string		suffix=outfile;
	std::transform(suffix.begin(), suffix.end(), suffix.begin(),
			[](unsigned char c) { return std::tolower(c); });
	const bool		geojson=suffix.size() > 5 &&
				(suffix.compare(suffix.size() - 5, 5, ".json") == 0 ||
				 (suffix.size() > 8 && suffix.compare(suffix.size() - 8, 8, ".geojson") == 0));

	std::unique_ptr<ProblemWriter>	writer;
	if (geojson) {
		writer=std::make_unique<GeoJSONWriter>(outfile, vm.count("verbose"));
	} else {
		writer=std::make_unique<SpatiaLiteWriter>(outfile, vm.count("verbose"));
	}

	try {
		rules.set_known_layers(writer->layer_names());
	} catch(const std::exception& e) {
		std::cerr << "Error in ruleset " << vm["rules"].as<std::string>() << ": " << e.what() << "\n";
		exit(-1);
	}

	WayHandler	handler(*writer, &rules);

	osmium::io::Reader reader{input_file};
	osmium::apply(reader, location_handler, handler);
	reader.close();
}
