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

// The type of index used. This must match the include file above
using index_type = osmium::index::map::FlexMem<osmium::unsigned_object_id_type, osmium::Location>;

// The location handler always depends on the index type
using location_handler_type = osmium::handler::NodeLocationsForWays<index_type>;

#define DEBUG 0

class SpatiaLiteWriter : public osmium::handler::Handler {
    gdalcpp::Layer		*m_layer_wayproblem;
    gdalcpp::Layer		*m_layer_footway;
    gdalcpp::Dataset		dataset;
    osmium::geom::OGRFactory<>	m_factory{};
public:
    explicit SpatiaLiteWriter(std::string &dbname) :
	dataset("sqlite", dbname, gdalcpp::SRS{}, {  "SPATIALITE=TRUE", "INIT_WITH_EPSG=no" }) {

		dataset.exec("PRAGMA synchronous = OFF");

		m_layer_wayproblem=addLineStringLayer("wayproblems");
	}

	gdalcpp::Layer *addLineStringLayer(const char *name) {
		gdalcpp::Layer *layer=new gdalcpp::Layer(dataset, name, wkbLineString);

		layer->add_field("id", OFTString, 20);
		layer->add_field("key", OFTString, 20);
		layer->add_field("value", OFTString, 20);
		layer->add_field("changeset", OFTString, 20);
		layer->add_field("user", OFTString, 20);
		layer->add_field("timestamp", OFTString, 20);
		layer->add_field("problem", OFTString, 60);
		layer->add_field("style", OFTString, 20);

		return layer;
	}

	void writeWay(const osmium::Way& way, const char *style, const char *problem) {
		try  {
			std::unique_ptr<OGRLineString> linestring = m_factory.create_linestring(way);

			gdalcpp::Feature feature{*m_layer_wayproblem, std::move(linestring)};

			feature.set_field("id", static_cast<double>(way.id()));
			feature.set_field("user", way.user());
			feature.set_field("changeset", static_cast<double>(way.changeset()));
			feature.set_field("timestamp", way.timestamp().to_iso().c_str());
			feature.set_field("problem", problem);
			feature.set_field("style", style);

			feature.add_to_layer();

			std::cout << " way " << way.id() << " " << problem << std::endl;

		} catch (gdalcpp::gdal_error) {
			std::cerr << "gdal_error while creating feature " << std::endl;
		} catch (osmium::invalid_location) {
			std::cerr << "invalid location" << std::endl;
		} catch (osmium::geometry_error) {
			std::cerr << "geometry error" << std::endl;
		}
	}
};

class extendedTagList  {
	const osmium::TagList&	taglist;

	const std::vector<std::string> motor_vehicle_highway{
			"residential", "unclassified", "tertiary",
			"secondary", "primary", "motorway"};

	const std::vector<std::string> highway_should_have_ref_list {
			"motorway", "motorway_link",
			"trunk", "trunk_link",
			"primary", "primary_link",
			"secondary", "secondary_link"};

	const std::vector<std::string> highway_may_have_ref_list {
			"motorway", "motorway_link",
			"trunk", "trunk_link",
			"primary", "primary_link",
			"secondary", "secondary_link",
			"tertiary", "tertiary_link"};

	const std::vector<std::string> highway_public {
			"motorway", "motorway_link",
			"trunk", "trunk_link",
			"primary", "primary_link",
			"secondary", "secondary_link",
			"tertiary", "tertiary_link",
			"unclassified", "residential"
			"living_street"};

	const std::vector<std::string> value_true_list {
			"yes", "true", "1" };

	const std::vector<std::string> value_false_list {
			"no", "false", "0" };

	public:
		extendedTagList(const osmium::TagList& tags) : taglist(tags) { }

		bool string_in_list(const char *string, const std::vector<std::string> &list) {
			if (!string)
				return false;
			if (find(list.begin(), list.end(), string) != list.end()) {
				return true;
			}
			return false;
		}

		bool highway_should_have_ref() {
			return string_in_list(get_value_by_key("highway"), highway_should_have_ref_list);
		}

		bool highway_may_have_ref() {
			return string_in_list(get_value_by_key("highway"), highway_may_have_ref_list);
		}

		const char *get_value_by_key(const char *key) {
			return taglist.get_value_by_key(key);
		}

		bool has_key(const char *key) {
			return taglist.has_key(key);
		}

		bool has_key_value(const char *key, const char *value) {
			const char	*tlvalue=taglist.get_value_by_key(key);
			if (!tlvalue)
				return 0;
			return (0 == strcmp(tlvalue, value));
		}

		bool key_value_is_true(const char *key) {
			return string_in_list(get_value_by_key(key), value_true_list);
		}

		bool key_value_is_false(const char *key) {
			return string_in_list(get_value_by_key(key), value_false_list);
		}

		bool is_motor_vehicle_road() {
			const char *highway=get_value_by_key("highway");
			return string_in_list(highway, motor_vehicle_highway);
		}

		bool is_public() {
			return string_in_list(get_value_by_key("highway"), highway_public);
		}
};

class WayHandler : public osmium::handler::Handler {
	SpatiaLiteWriter	&writer;

	public:
		WayHandler(SpatiaLiteWriter &writer) : writer(writer) {};

		void way(osmium::Way& way) {
			extendedTagList	taglist(way.tags());

			if (!taglist.has_key("highway")) {
				return;
			}

			if (taglist.highway_should_have_ref()) {
				if (!taglist.has_key_value("junction", "roundabout")) {
					if (!taglist.has_key("ref")) {
						writer.writeWay(way, "ref", "highway should have ref");
					}
				}
			}

			if (!taglist.highway_may_have_ref()) {
				if (taglist.has_key("ref")) {
					writer.writeWay(way, "ref", "highway should not have ref");
				}
			}

			{
				std::vector<const char *>	accesstags={
					"access", "vehicle", "motor_vehicle", "motorcycle",
					"motorcar", "hgv", "psv", "bicycle", "foot",
					"goods", "mofa", "moped", "horse"};

				if (taglist.is_public()) {
					for(auto key : accesstags) {
						const char *value=taglist.get_value_by_key(key);
						if (!value)
							continue;
						if (!strcmp(value, "permissive")) {
							writer.writeWay(way, "default", "public highway cant have permissive access tags");
						} else if (!strcmp(value, "private")) {
							writer.writeWay(way, "default", "public highway cant have private access tags");
						} else if (!strcmp(value, "customers")) {
							writer.writeWay(way, "default", "public highway cant have customers access tags");
						}
					}
				}

			}

			if (taglist.has_key("goods")) {
				writer.writeWay(way, "default", "goods=* is not in use in Germany - no distinction between goods and hgv");
			}
			if (taglist.has_key("access")) {
				if (taglist.key_value_is_false("access")) {
					writer.writeWay(way, "default", "access=no - Nicht StVO konform. Vermutlich motor_vehicle=no oder vehicle=no");
				} else if (taglist.key_value_is_true("access")) {
					writer.writeWay(way, "redundant", "access=yes - vermutlich redundant");
				} else if (taglist.has_key_value("access", "destination")) {
					writer.writeWay(way, "default", "access=destination nicht StVO konform. Vermutlich vehicle=destination oder motor_vehicle=destination");
				}
			}

			if (taglist.has_key_value("highway", "service")) {
				if (taglist.has_key("name")) {
					writer.writeWay(way, "default", "highway=service with name=* is suspicious - Either public e.g. not service or name tag abuse");
				}
			}

			if (taglist.has_key_value("highway", "track")) {
				if (taglist.has_key("name")) {
					writer.writeWay(way, "default", "highway=track with name is suspicious - probably not track");
				}
				if (taglist.has_key("maxspeed")) {
					writer.writeWay(way, "default", "highway=track with maxspeed is suspicious - probably not track");
				}
			}

			if (taglist.has_key_value("highway", "footway")) {
				if (!taglist.has_key("bicycle")) {
					writer.writeWay(way, "footway", "highway=footway without bicycle=yes/no tag - suspicious combination");
				}

				if (taglist.key_value_is_true("foot")) {
					writer.writeWay(way, "redundant", "highway=footway with foot=yes is redundant");
				}
				if (taglist.key_value_is_false("foot")) {
					writer.writeWay(way, "default", "highway=footway with foot=no is broken");
				}
			}

			if (taglist.has_key_value("highway", "living_street")) {
				if (taglist.has_key("maxspeed")) {
					writer.writeWay(way, "default", "maxspeed on living_street is broken - neither numeric nor walk is correct");
				}

				if (taglist.key_value_is_false("bicycle")) {
					writer.writeWay(way, "default", "living_street with bicycle=no is broken");
				}
				if (taglist.key_value_is_true("bicycle")) {
					writer.writeWay(way, "redundant", "living_street with bicycle=yes is redundant");
				}

				if (taglist.key_value_is_false("foot")) {
					writer.writeWay(way, "default", "foot=no on living_street is broken");
				}
				if (taglist.key_value_is_true("foot")) {
					writer.writeWay(way, "redundant", "foot=yes on living_street is redundant");
				}

				if (taglist.key_value_is_false("vehicle")) {
					writer.writeWay(way, "default", "vehicle=no on living_street is broken");
				}
				if (taglist.key_value_is_true("vehicle")) {
					writer.writeWay(way, "redundant", "vehicle=yes on living_street is redundant");
				}

			}

			if (taglist.has_key_value("highway", "cycleway")) {
				if (taglist.key_value_is_true("bicycle")) {
					writer.writeWay(way, "redundant", "bicycle=yes on cycleway is redundant");
				}
				if (taglist.key_value_is_false("bicycle")) {
					writer.writeWay(way, "default", "bicycle=no on cycleway is broken");
				}
				if (taglist.has_key_value("vehicle", "yes")) {
					writer.writeWay(way, "default", "vehicle=yes on cycleway is broken as its not a cycleway");
				}
				if (taglist.has_key_value("vehicle", "no")) {
					writer.writeWay(way, "default", "vehicle=no on cycleway is broken as bicycle is a vehicle");
				}
			}

			if (taglist.key_value_is_true("vehicle")) {
				if (taglist.key_value_is_false("motor_vehicle")) {
					writer.writeWay(way, "default", "vehicle=yes and motor_vehicle=no should be bicyle");
				} else if (taglist.key_value_is_true("motor_vehicle")) {
					writer.writeWay(way, "redundant", "vehicle=yes and motor_vehicle=yes is redundant");
				}
			}

			if (taglist.key_value_is_true("motor_vehicle")) {
				if (taglist.key_value_is_false("motorcycle")) {
					writer.writeWay(way, "default", "motor_vehicle=yes and motorcycle=no should be motorcar + hgv");
				} else if (taglist.key_value_is_true("motorcycle")) {
					writer.writeWay(way, "redundant", "motor_vehicle=yes and motorcycle=yes is redundant");
				}

				if (taglist.key_value_is_false("motorcar")) {
					writer.writeWay(way, "default", "motor_vehicle=yes and motorcar=no should be motorcycle");
				} else if (taglist.key_value_is_true("motorcar")) {
					writer.writeWay(way, "redundant", "motor_vehicle=yes and motorcar=yes is redundant");
				}

				if (taglist.key_value_is_false("hgv")) {
					writer.writeWay(way, "default", "motor_vehicle=yes and hgv=no should be motorcar");
				} else if (taglist.key_value_is_true("hgv")) {
					writer.writeWay(way, "redundant", "motor_vehicle=yes and hgv=yes is redundant");
				}
			}

			if (taglist.key_value_is_false("tunnel")) {
				writer.writeWay(way, "redundant", "tunnel=no ist redundant");
			}
			if (taglist.key_value_is_false("construction")) {
				writer.writeWay(way, "redundant", "construction=no ist redundant");
			}
			if (taglist.key_value_is_false("oneway")) {
				writer.writeWay(way, "redundant", "oneway=no ist redundant");
			}

			if (taglist.has_key_value("junction", "roundabout")) {
				if (taglist.has_key("name")) {
					writer.writeWay(way, "default", "name on roundabout is most like an error");
				}
				if (taglist.has_key("ref")) {
					writer.writeWay(way, "default", "ref on roundabout is most like an error");
				}
				if (taglist.has_key("oneway")) {
					writer.writeWay(way, "redundant", "oneway on roundabout is redundant");
				}
			}
		}
};


namespace po = boost::program_options;

int main(int argc, char* argv[]) {

	po::options_description         desc("Allowed options");
        desc.add_options()
                ("help,h", "produce help message")
                ("infile,i", po::value<std::string>(), "Input file")
		("dbname,d", po::value<std::string>(), "Output database name")
        ;
        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);
	po::notify(vm);

	// Initialize an empty DynamicHandler. Later it will be associated
	// with one of the handlers. You can think of the DynamicHandler as
	// a kind of "variant handler" or a "pointer handler" pointing to the
	// real handler.
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

	// On the second pass we read all objects and run them first through the
	// node location handler and then the multipolygon collector. The collector
	// will put the areas it has created into the "buffer" which are then
	// fed through our "handler".
	WayHandler	handler(writer);

	osmium::io::Reader reader{input_file};
	osmium::apply(reader, location_handler, handler);
	reader.close();
	std::cerr << "Pass 2 done\n";

}

