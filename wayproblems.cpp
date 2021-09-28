#include <cstdlib>  // for std::exit
#include <cstring>  // for std::strcmp
#include <iostream> // for std::cout, std::cerr
#include <limits>     // for Nan

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
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

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
	layermax
};

class SpatiaLiteWriter : public osmium::handler::Handler {
	std::array<gdalcpp::Layer *, layermax>	layer;
	std::array<std::string, layermax>	layername;

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
			addLineStringLayer(L_DEFAULTS, "defaults");
		}

	void addLineStringLayer(const int layerid, const char *name) {
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

		layername[layerid]=name;
		layer[layerid]=l;
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

class extendedTagList  {
	const osmium::TagList&	taglist;

	const std::map<const std::string, const std::string> maxspeed_type_to_speed {
			{ "DE:zone30", "30" },
			{ "DE:zone:30", "30" },
			{ "DE:zone20", "20" },
			{ "DE:zone:20", "20" },
			{ "DE:zone10", "10" },
			{ "DE:zone:10", "10" },
			{ "DE:bicycle_road", "30" },
			{ "DE:urban", "50" },
			{ "DE:rural", "100" }
	};

	const std::vector<std::string> highway_should_have_ref_list {
		"motorway",
		"trunk",
		"primary",
		"secondary"
	};

	const std::vector<std::string> highway_may_have_ref_list {
		"motorway",
		"trunk",
		"primary",
		"secondary",
		"tertiary"
	};

	const std::vector<std::string> highway_motorway_list {
		"motorway", "motorway_link"
	};

	const std::vector<std::string> highway_public_list {
		"motorway", "motorway_link",
		"trunk", "trunk_link",
		"primary", "primary_link",
		"secondary", "secondary_link",
		"tertiary", "tertiary_link",
		"unclassified", "residential"
		"living_street"
	};
	const std::vector<std::string> value_true_list {
			"yes", "true", "1" };

	const std::vector<std::string> value_false_list {
			"no", "false", "0" };

	public:
		extendedTagList(const osmium::TagList& tags) : taglist(tags) { }

		const char* operator[](const char* key) const noexcept {
			return taglist.get_value_by_key(key);
		}

		bool string_in_list(const char *string, const std::vector<std::string> &list) {
			if (!string)
				return false;
			if (find(list.begin(), list.end(), string) != list.end()) {
				return true;
			}
			return false;
		}

		bool key_value_in_list(const char *key, const std::vector<std::string> &list) {
			const char *value=get_value_by_key(key);
			if (!value)
				return false;
			return string_in_list(value, list);
		}


		double key_value_as_double(const char *key) {
			double result=std::numeric_limits<double>::quiet_NaN();
			try {
				result=std::stof(get_value_by_key(key), nullptr);
			} catch(const std::invalid_argument& e) {
			}
			return result;
		}

		bool key_value_is_double(const char *key) {
			return !std::isnan(key_value_as_double(key));
		}

		int key_value_as_int(const char *key) {
			int result=std::numeric_limits<int>::max();
			try {
				const char	*value=get_value_by_key(key);
				size_t		pos;

				result=std::stoi(value, &pos);

				if (pos != strlen(value))
					return std::numeric_limits<int>::max();
			} catch(const std::invalid_argument& e) {
			}
			return result;
		}

		bool key_value_is_int(const char *key) {
			return key_value_as_int(key) != std::numeric_limits<int>::max();
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

		bool road_is_public() {
			return string_in_list(get_value_by_key("highway"), highway_public_list);
		}

		bool road_is_motorway() {
			return string_in_list(get_value_by_key("highway"), highway_motorway_list);
		}

		const char *maxspeed_from_maxspeed_type_tag(const char *typetag) {
			const char *maxspeedtype=get_value_by_key(typetag);

			if (!maxspeedtype)
				return nullptr;

			auto maxspeed = maxspeed_type_to_speed.find(maxspeedtype);

			if (maxspeed == maxspeed_type_to_speed.end())
				return nullptr;

			return maxspeed->second.c_str();
		}
};


class WayHandler : public osmium::handler::Handler {
	SpatiaLiteWriter	&writer;

	public:
		WayHandler(SpatiaLiteWriter &writer) : writer(writer) {};

		void circular_way(osmium::Way& way, extendedTagList& taglist) {
			if (way.ends_have_same_id()) {
				if (!taglist.has_key_value("area", "yes")
					&& !taglist.has_key_value("junction", "roundabout")
					&& taglist.key_value_in_list("highway", { "tertiary", "secondary",
						"primary", "unclassified", "residential" })) {
					writer.writeWay(L_STRANGE, way, "default", "Circular way without junction=roundabout");
				}
			} else {
				if (taglist.has_key_value("area", "yes")) {
					writer.writeWay(L_WP, way, "default", "area=yes on unclosed way");
				}
			}
		}

		void tag_layer(osmium::Way& way, extendedTagList& taglist) {
			if (!taglist.has_key("layer")) {
				return;
			}

			if (!taglist.key_value_is_int("layer")) {
				writer.writeWay(L_WP, way, "default", "layer=%s is not integer", taglist.get_value_by_key("layer"));
			} else {
				int layer=taglist.key_value_as_int("layer");
				if (layer == 0) {
					writer.writeWay(L_DEFAULTS, way, "redundant", "layer=%s is default", taglist.get_value_by_key("layer"));
				} else if (layer > 10) {
					writer.writeWay(L_WP, way, "redundant", "layer=%s where num > 10 seems broken", taglist.get_value_by_key("layer"));
				} else if (layer < -10) {
					writer.writeWay(L_WP, way, "redundant", "layer=%s where num < -10 seems broken", taglist.get_value_by_key("layer"));
				}
			}
		}

		void tag_ref(osmium::Way& way, extendedTagList& taglist) {
			if (taglist.highway_should_have_ref()) {
				if (!taglist.has_key_value("junction", "roundabout")) {
					if (!taglist.has_key("ref")) {
						writer.writeWay(L_REF, way, "ref", "highway should have ref");
					}
				}
			}

			if (!taglist.highway_may_have_ref()) {
				if (!taglist.has_key_value("highway", "path")) {
					if (taglist.has_key("ref")) {
						writer.writeWay(L_REF, way, "ref", "highway should not have ref");
					}
				}
			}

			if (taglist.key_value_in_list("ref", { "-", "+", "*", ".", "_", " ", "\t", "#" })) {
				writer.writeWay(L_REF, way, "ref", "ref=%s seems broken", taglist.get_value_by_key("ref"));
				writer.writeWay(L_WP, way, "ref", "ref=%s seems broken", taglist.get_value_by_key("ref"));
			}
		}

		void tag_maxspeed_source(osmium::Way& way, extendedTagList& taglist) {
			if (!taglist.has_key("maxspeed:source"))
				return;

			writer.writeWay(L_WP, way, "steelline", "maxspeed:source should be source:maxspeed or maxspeed:type");
		}

		bool maxspeed_valid_source(extendedTagList& taglist, const char *tag) {
			return taglist.key_value_in_list(tag, {
					"sign", "signals",
					"DE:motorway", "DE:urban", "DE:rural",
					"DE:zone", "DE:bicycle_road",
					"DE:zone30", "DE:zone:30",
					"DE:zone20", "DE:zone:20",
					"DE:zone10", "DE:zone:10",
				});
		}

		void tag_maxspeed_type(osmium::Way& way, extendedTagList& taglist) {
			if (!taglist.has_key("maxspeed:type"))
				return;

			if (!maxspeed_valid_source(taglist, "maxspeed:type")) {
				writer.writeWay(L_WP, way, "steelline", "maxspeed:type=%s is unknown",
					taglist.get_value_by_key("maxspeed:type"));
			}

			maxspeed_check_against_type(way, taglist, "maxspeed:type");
		}

		void tag_source_maxspeed(osmium::Way& way, extendedTagList& taglist) {
			if (!taglist.has_key("source:maxspeed"))
				return;

			if (!maxspeed_valid_source(taglist, "source:maxspeed")) {
				writer.writeWay(L_WP, way, "steelline", "source:maxspeed=%s is unknown",
					taglist.get_value_by_key("source:maxspeed"));
			}

			maxspeed_check_against_type(way, taglist, "source:maxspeed");
		}

		void maxspeed_check_against_type(osmium::Way& way, extendedTagList& taglist, const char *origin) {
			auto maxspeedfromtype=taglist.maxspeed_from_maxspeed_type_tag(origin);

			if (maxspeedfromtype == nullptr)
				return;

			if (taglist.has_key("maxspeed")) {
				auto maxspeed=taglist.get_value_by_key("maxspeed");
				if (0 != std::strcmp(maxspeed, maxspeedfromtype)) {
					writer.writeWay(L_WP, way, "steelline", "%s=%s is %s but maxspeed contains %s",
						origin,
						taglist.get_value_by_key(origin),
						maxspeedfromtype,
						maxspeed);

					// std::cerr << "Mismatch " << maxspeed << " from type " << maxspeedfromtype << std::endl;
				}


			} else {
				writer.writeWay(L_WP, way, "steelline", "%s=%s is %s but no maxspeed",
					origin, taglist.get_value_by_key(origin),
					maxspeedfromtype);

				// std::cerr << "No maxspeed - from type " << maxspeedfromtype << std::endl;
			}
		}

		void tag_maxspeed(osmium::Way& way, extendedTagList& taglist) {
			/*
			 * Maxspeed
			 *
			 */

			const std::vector<const char *>	maxspeedtags={ "maxspeed", "maxspeed:forward", "maxspeed:backward" };
			const std::vector<const char *>	vehicles={ "", ":hgv", ":vehicle", ":motor_vehicle", ":bus" };
			for(auto maxspeedtag : maxspeedtags) {
				for(auto vehicle : vehicles) {
					std::string	key=maxspeedtag;
					key.append(vehicle);

					if (!taglist.has_key(key.c_str()))
						continue;

					if (taglist.key_value_in_list(key.c_str(), { "none", "signals" }))
						continue;

					try {
						std::stoi(taglist.get_value_by_key(key.c_str()), nullptr, 10);
					} catch(const std::invalid_argument& e) {
						writer.writeWay(L_WP, way, "steelline", "%s=%s is not numerical",
								key.c_str(), taglist[key.c_str()]);
					}
					// TODO - Integer/Decimal/Float?
					// TODO - mph/knots/kmh
					// TODO - > 120?
				}
			}

			if (taglist.has_key("maxspeed") && (
					 taglist.has_key("maxspeed:forward")
					 || taglist.has_key("maxspeed:backward")
					)) {
				writer.writeWay(L_WP, way, "steelline", "maxspeed and maxspeed:forward/backward - overlapping values");
			}
		}

		void tag_maxheight(osmium::Way& way, extendedTagList& taglist) {
			/*
			 * Maxheight
			 */
			if (!taglist.has_key("maxheight"))
				return;

			if (taglist.key_value_in_list("maxheight", { "default", "none", "unsigned", "no_sign", "no_indications", "below_default" }))
				return;

			if (!taglist.key_value_is_double("maxheight")) {
				writer.writeWay(L_WP, way, "default", "maxheight=%s is not float",
					taglist["maxheight"]);
			} else {
				double maxheight=taglist.key_value_as_double("maxheight");
				if (maxheight < 1.8) {
					// https://www.openstreetmap.org/way/25048948
					writer.writeWay(L_WP, way, "default", "maxheight=%s is less than 1.8",
						taglist["maxheight"]);
					// TODO - Maxheight for general traffic - parking access might be lower
				} else if (maxheight > 7) {
					// https://www.openstreetmap.org/way/25363727
					writer.writeWay(L_WP, way, "default", "maxheight=%s is more than 7 - suspicous value",
						taglist["maxheight"]);
				}
			}
		}

		void tag_type(osmium::Way& way, extendedTagList& taglist) {
			if (!taglist.has_key("type"))
				return;

			if (taglist.has_key_value("type", "route")) {
				writer.writeWay(L_WP, way, "default", "type=%s is defined for route relations not ways",
					taglist["type"]);
			} else {
				writer.writeWay(L_STRANGE, way, "default", "type=%s is strange",
					taglist["type"]);
			}
		}


		void tag_maxwidth(osmium::Way& way, extendedTagList& taglist) {
			if (!taglist.has_key("maxwidth"))
				return;

			if (!taglist.key_value_is_double("maxwidth")) {
				writer.writeWay(L_WP, way, "default", "maxwidth=%s is not float",
					taglist["maxwidth"]);
			} else {
				double maxwidth=taglist.key_value_as_double("maxwidth");
				if (maxwidth < 1.8) {
					writer.writeWay(L_WP, way, "default", "maxwidth=%s is less than 1.8",
						taglist["maxwidth"]);
				} else if (maxwidth > 7) {
					writer.writeWay(L_WP, way, "default", "maxwidth=%s is more than 7 - suspicous value",
						taglist["maxwidth"]);
				}
			}
		}

		void tag_lanes(osmium::Way& way, extendedTagList& taglist) {
			/*
			 * Lanes
			 *
			 */
			const std::vector<const char *>	lanetags={ "lanes", "lanes:forward", "lanes:backward" };
			for(auto key : lanetags) {
				if (!taglist.has_key(key))
					continue;

				if (!taglist.key_value_is_int(key)) {
					writer.writeWay(L_WP, way, "default", "%s=%s is not integer",
							key, taglist.get_value_by_key(key));
				} else {
					int lanes=taglist.key_value_as_int(key);

					if (lanes<=0) {
						writer.writeWay(L_WP, way, "default", "%s=%s is less or equal 0",
								key, taglist.get_value_by_key(key));
					} else if (lanes > 8) {
						writer.writeWay(L_WP, way, "default", "%s=%s is more than 8 - suspicious value",
								key, taglist.get_value_by_key(key));
					}
				}

				// TODO - if there is only lanes + lanes:forward we calculate lanes:backward and vice versa

				std::vector<const char *>	lanekeypreps={ "turn:", "destination:" };
				for(auto lanekeyprep : lanekeypreps) {
					std::string	lanekey=lanekeyprep;
					lanekey.append(key);

					if (taglist.has_key(lanekey.c_str())) {
						int lanes=taglist.key_value_as_int(key);
						const char *tlanes=taglist.get_value_by_key(lanekey.c_str());
						int num=0;
						while((tlanes=strstr(tlanes,"|")) != NULL) {
							tlanes++;
							num++;
						}
						if (lanes != (num+1)) {
							writer.writeWay(L_WP, way, "default", "%s=%d does not match elements in %s=%s",
									key, lanes, lanekey.c_str(), taglist.get_value_by_key(lanekey.c_str()));
						}
					}
				}

				std::string			turnkey="turn:";
				turnkey.append(key);

				if (taglist.has_key(turnkey.c_str())) {
					std::string			turnlanes=taglist.get_value_by_key(turnkey.c_str());
					std::vector<std::string>	turnlanetypes;

					boost::split(turnlanetypes, turnlanes, boost::is_any_of("|;"), boost::token_compress_on);

					std::vector<std::string>	validturntypes={ "left", "right", "slight_left", "slight_right",
						"through", "merge_to_left", "merge_to_right", "reverse", "none", "sharp_left", "sharp_right", "" };

					for(auto turntype : turnlanetypes) {
						if (find(validturntypes.begin(), validturntypes.end(), turntype) == validturntypes.end()) {
							writer.writeWay(L_WP, way, "default", "%s=%s contains lane turn %s which is unknown",
									key, turnlanes.c_str(), turntype.c_str());
						}
					}
				}

				// TODO turn:lanes are from left to right. So in in Germany the first element cant be
				// right if one of the next elements is less than right as that means crossing lanes
				//
				// We might assign "turn rate" and the next element must have a lower or same turn rate
				//
				// reverse	-> 40
				// sharp_left	-> 20
				// left		-> 18
				// slight_left	-> 16
				// through	-> 10	none, merge_to_left, merge_to_right
				// slight_right	->  8
				// right	->  6
				// sharp_right	->  4
			}


			if (taglist.has_key("lanes") && taglist.has_key("lanes:forward") && taglist.has_key("lanes:backward")) {

				int lanes=taglist.key_value_as_int("lanes");
				int lanesfwd=taglist.key_value_as_int("lanes:forward");
				int lanesbck=taglist.key_value_as_int("lanes:backward");

				if (lanes != (lanesfwd + lanesbck)) {
					writer.writeWay(L_WP, way, "default", "lanes=%d does not match sum of lanes:backward=%d and lanes:forward=%d",
							lanes, lanesfwd, lanesbck);
				}
			}
		}

		void tag_sidewalk(osmium::Way& way, extendedTagList& taglist) {
			if (!taglist.has_key("sidewalk"))
				return;

			if (!taglist.key_value_in_list("sidewalk", { "both", "left", "right", "none", "no", "yes", "separate" })) {
				writer.writeWay(L_WP, way, "default", "sidewalk=%s not in known value list", taglist.get_value_by_key("sidewalk"));
			}

			/* sidewalk on motorway or trunk */
			if (taglist.key_value_in_list("sidewalk", { "both", "left", "right", "yes" })) {
				// Removed trunk_link - seems valid: Example https://www.openstreetmap.org/way/10871357
				if (taglist.key_value_in_list("highway", { "motorway", "motorway_link", "trunk" })) {
					writer.writeWay(L_WP, way, "default", "highway=%s and sidewalk=%s - most likely an error",
						taglist.get_value_by_key("highway"),taglist.get_value_by_key("sidewalk"));
				}
				/* sidewalk on motorroad */
				if (taglist.key_value_is_true("motorroad")) {
					writer.writeWay(L_WP, way, "default", "motorroad=%s and sidewalk=%s - most likely an error",
						taglist.get_value_by_key("motorroad"),taglist.get_value_by_key("sidewalk"));
				}
			}

			// TODO - Sidewalk on cycleway, footway, path, track
		}

		void tag_segregated(osmium::Way& way, extendedTagList& taglist) {
			if (!taglist.has_key("segregated"))
				return;

			if (!taglist.key_value_in_list("highway", { "footway", "cycleway", "path" })) {
				writer.writeWay(L_CYCLING, way, "default", "highway=%s and segregated=%s - segregated only used on foot/cycleway and path",
					taglist.get_value_by_key("highway"),taglist.get_value_by_key("segregated"));
			}
			if (!taglist.key_value_in_list("segregated", { "yes", "no" })) {
				writer.writeWay(L_WP, way, "default", "segregated=%s - value not in known value list",
					taglist.get_value_by_key("segregated"));
			}
		}

		void tag_shoulder(osmium::Way& way, extendedTagList& taglist) {
			if (taglist.has_key("shoulder")) {
				if (!taglist.key_value_in_list("shoulder", { "both", "left", "right", "no", "yes" })) {
					writer.writeWay(L_WP, way, "default", "shoulder=%s not in known value list", taglist.get_value_by_key("shoulder"));
				}

				if (taglist.key_value_in_list("highway", { "path", "footway", "cycleway", "track", "steps", "pedestrian", "bridleway" })) {
					writer.writeWay(L_WP, way, "default", "highway=%s should not have shoulder=%s",
						taglist.get_value_by_key("highway"), taglist.get_value_by_key("shoulder"));
				}
			}
		}

		void node_only_tags(osmium::Way& way, extendedTagList& taglist) {
			if (taglist.has_key("noexit")) {
				writer.writeWay(L_WP, way, "default", "noexit=* should only be used on nodes");
			}

			if (taglist.key_value_in_list("highway", { "stop", "give_way",
					"street_lamp", "traffic_lights", "traffic_calming",
					"traffic_mirror", "speed_camera", "passing_place",
					"mini_roundabout", "emergency_access_point", "bus_stop"})) {

				writer.writeWay(L_WP, way, "default", "highway=%s should only be used on nodes",
						taglist.get_value_by_key("highway"));

			}
		}

		void tag_oneway(osmium::Way& way, extendedTagList& taglist) {

			if (taglist.key_value_is_false("oneway")) {
				writer.writeWay(L_DEFAULTS, way, "redundant", "oneway=no is default");
			}


			/* Elements which only make sense on ANY oneway */
			if (!taglist.has_key("oneway") || taglist.key_value_in_list("oneway", { "0", "no" })) {
				std::vector<const char *>	lanekey={ "turn:lanes", "destination", "destination:lanes" };
				for(auto key : lanekey) {
					if (taglist.has_key(key)) {
						writer.writeWay(L_WP, way, "default", "%s makes only sense on oneway streets", key);
					}
				}

				std::vector<const char *>	cyclewaykeys={ "cycleway", "cycleway:left", "cycleway:right" };
				for(auto key : cyclewaykeys) {
					if (taglist.key_value_in_list(key, { "opposite", "opposite_lane", "opposite_track", "opposite_share_busway" })) {
						writer.writeWay(L_CYCLING, way, "default", "%s=%s makes only sense on oneway streets",
								key, taglist.get_value_by_key(key));
					}
				}
			}

			if (taglist.has_key("oneway")) {
				/* Elements which dont make sense on oneway in ways direction*/
				if (taglist.key_value_in_list("oneway", { "true", "yes", "1" })) {
					std::vector<const char *>	keys={ "turn:lanes:backward", "destination:backward", "destination:lanes:backward", "maxspeed:backward" };
					for(auto key : keys) {
						if (taglist.has_key(key)) {
							writer.writeWay(L_WP, way, "default", "%s on oneway=%s makes no sense",
								key, taglist.get_value_by_key("oneway"));
						}
					}

				}

				/* Elements which dont make sense on reversed oneway */
				if (taglist.key_value_in_list("oneway", { "-1" })) {
					std::vector<const char *>	keys={ "turn:lanes:forward", "destination:forward", "destination:lanes:forward", "maxspeed:forward" };
					for(auto key : keys) {
						if (taglist.has_key(key)) {
							writer.writeWay(L_WP, way, "default", "%s on oneway=%s makes no sense",
								key, taglist.get_value_by_key("oneway"));
						}
					}
				}
			}
		}

		bool construction_osrm_whitelist(extendedTagList& taglist) {
			return taglist.key_value_in_list("construction", { "no", "widening", "minor" });
		}

		void tag_proposed(osmium::Way& way, extendedTagList& taglist) {
			if (!taglist.has_key("proposed"))
				return;

			if (!taglist.has_key("highway"))
				return;

			writer.writeWay(L_WP, way, "default", "proposed=%s on highway=%s causes OSRM to avoid road",
					taglist.get_value_by_key("highway"),
					taglist.get_value_by_key("construction"));
		}

		void tag_construction(osmium::Way& way, extendedTagList& taglist) {
			if (!taglist.has_key("construction"))
				return;

			if (taglist.has_key_value("construction", "yes")) {
				writer.writeWay(L_WP, way, "redundant", "construction=yes is deprecated");
			} else if (taglist.has_key_value("construction", "no")) {
				writer.writeWay(L_DEFAULTS, way, "redundant", "construction=no is default");
			}

			if (!taglist.key_value_in_list("construction", {
					"yes", "no", "widening", "minor",
					"motorway", "motorway_link", "trunk", "trunk_link",
					"primary", "primary_link", "secondary", "secondary_link",
					"tertiary", "tertiary_link", "unclassified",
					"residential", "pedestrian", "service", "track", "cycleway", "footway",
					"steps", "path" })) {
				writer.writeWay(L_WP, way, "default", "construction=%s not in known list", taglist.get_value_by_key("construction"));
			}

			if (!taglist.has_key_value("highway", "construction")
					&& !construction_osrm_whitelist(taglist)) {
				writer.writeWay(L_WP, way, "default", "construction=%s on highway=%s",
						taglist.get_value_by_key("highway"),
						taglist.get_value_by_key("construction"));
			}
		}

		void tag_tracktype(osmium::Way& way, extendedTagList& taglist) {
			if (!taglist.has_key("tracktype"))
				return;

			if (!taglist.has_key_value("highway", "track")) {
				writer.writeWay(L_WP, way, "brownline", "tracktype=* on non track");
			}

			if (!taglist.key_value_in_list("tracktype", { "grade1", "grade2", "grade3", "grade4", "grade5" })) {
				writer.writeWay(L_WP, way, "brownline", "tracktype=%s is unknown",
					taglist.get_value_by_key("tracktype"));
			}

			if (taglist.has_key("surface")) {
				if (taglist.has_key_value("tracktype", "grade1")) {
					if (!taglist.key_value_in_list("surface",
							{ "paved", "cobblestone", "asphalt", "asphalt:lanes",
							"paving_stones", "concrete", "concrete:lanes" })) {
						writer.writeWay(L_WP, way, "brownline", "tracktype=%s with surface=%s is an suspicious combination",
							taglist.get_value_by_key("tracktype"),
							taglist.get_value_by_key("surface"));
					}
				}

				if (taglist.key_value_in_list("tracktype", { "grade3", "grade4", "grade5" })) {
					if (taglist.key_value_in_list("surface",
							{ "paved", "cobblestone", "asphalt", "asphalt:lanes",
							"paving_stones", "concrete", "concrete:lanes" })) {
						writer.writeWay(L_WP, way, "brownline", "tracktype=%s with surface=%s is a suspicious combination",
							taglist.get_value_by_key("tracktype"),
							taglist.get_value_by_key("surface"));
					}
				}
			}
		}

		void tag_tunnel(osmium::Way& way, extendedTagList& taglist) {
			if (taglist.key_value_is_false("tunnel")) {
				writer.writeWay(L_DEFAULTS, way, "redundant", "tunnel=no ist default");
			}
		}

		void tag_junction(osmium::Way& way, extendedTagList& taglist) {
			if (taglist.has_key_value("junction", "roundabout")) {
				if (taglist.has_key("name")) {
					writer.writeWay(L_WP, way, "default", "name on roundabout is most likely an error - should not carry name or any street");
				}
				if (taglist.has_key("ref")) {
					writer.writeWay(L_WP, way, "default", "ref on roundabout is most likely an error - should not carry ref of any street");
				}
				if (taglist.has_key("oneway")) {
					writer.writeWay(L_DEFAULTS, way, "redundant", "oneway on roundabout is default");
				}
				if (taglist.key_value_in_list("sidewalk", { "both", "yes", "left" })) {
					writer.writeWay(L_WP, way, "default", "sidewalk=%s on roundabout - Right hand drive countries should have only a right sidewalk",
							taglist.get_value_by_key("sidewalk"));
				}
				if (taglist.key_value_in_list("cycleway", { "opposite", "opposite_lane", "opposite_track" })) {
					writer.writeWay(L_CYCLING, way, "default", "cycleway=%s on roundabout is broken",
							taglist.get_value_by_key("cycleway"));
				}
			}

			// TODO - junction compare against list
			// TODO - check for turn direction
		}

		void tag_bicycle(osmium::Way& way, extendedTagList& taglist) {
			const char *highway=taglist.get_value_by_key("highway");

			if (taglist.has_key("bicycle")) {
				const char *bikevalue=taglist.get_value_by_key("bicycle");

				if (taglist.road_is_public() && !taglist.road_is_motorway()) {
					if (taglist.key_value_is_true("bicycle")) {
						writer.writeWay(L_DEFAULTS, way, "redundant", "bicycle=%s on highway=%s is default", bikevalue, highway);
						writer.writeWay(L_CYCLING, way, "redundant", "bicycle=%s on highway=%s is default", bikevalue, highway);
					} else if (taglist.has_key_value("bicycle", "permissive")) {
						writer.writeWay(L_DEFAULTS, way, "redundant", "bicycle=designated on highway=%s is default - road is public", highway);
						writer.writeWay(L_CYCLING, way, "redundant", "bicycle=designated on highway=%s is default - road is public", highway);
					} else if (taglist.has_key_value("bicycle", "private")) {
						writer.writeWay(L_CYCLING, way, "default", "bicycle=%s on highway=%s is broken - road is public", bikevalue, highway);
					} else if (taglist.has_key_value("bicycle", "customers")) {
						writer.writeWay(L_CYCLING, way, "default", "bicycle=%s on highway=%s is broken - road is public", bikevalue, highway);
					} else if (taglist.has_key_value("bicycle", "destination")) {
						writer.writeWay(L_CYCLING, way, "default", "bicycle=%s on highway=%s is suspicious - StVO would allow vehicle=destination", bikevalue, highway);
					}
				}

				if (taglist.key_value_in_list("highway", { "track", "service" })) {
					if (taglist.key_value_is_true("bicycle")) {
						writer.writeWay(L_DEFAULTS, way, "redundant", "bicycle=%s on highway=%s is redundant", bikevalue, highway);
						writer.writeWay(L_CYCLING, way, "redundant", "bicycle=%s on highway=%s is redundant", bikevalue, highway);
					}
				}

				if (taglist.key_value_in_list("highway", { "trunk", "trunk_link", "motorway", "motorway_link" })) {
					if (taglist.key_value_in_list("bicycle", { "no", "0", "false" })) {
						writer.writeWay(L_DEFAULTS, way, "redundant", "bicycle=%s on highway=%s is default", bikevalue, highway);
						writer.writeWay(L_CYCLING, way, "redundant", "bicycle=%s on highway=%s is default", bikevalue, highway);
					} else {
						writer.writeWay(L_CYCLING, way, "default", "bicycle=%s on highway=%s is broken", bikevalue, highway);
					}
				}

				if (!taglist.key_value_in_list("bicycle", { "yes", "no", "private", "permissive",
						"destination" , "designated", "use_sidepath", "dismount" })) {
					writer.writeWay(L_CYCLING, way, "default", "bicycle=%s on highway=%s", bikevalue, highway);
				}
			}
		}

		void tag_foot(osmium::Way& way, extendedTagList& taglist) {
			const char *highway=taglist.get_value_by_key("highway");
			if (taglist.has_key("foot")) {
				const char *footvalue=taglist.get_value_by_key("foot");

				if (taglist.road_is_public() && !taglist.road_is_motorway()) {
					if (taglist.key_value_is_true("foot")) {
						writer.writeWay(L_DEFAULTS, way, "redundant", "foot=%s on highway=%s is default", footvalue, highway);
					} else if (taglist.has_key_value("foot", "permissive")) {
						writer.writeWay(L_WP, way, "default", "foot=yes on highway=%s is default", highway);
					} else if (taglist.has_key_value("foot", "private")) {
						writer.writeWay(L_WP, way, "default", "foot=%s on highway=%s is broken - road is public", footvalue, highway);
					} else if (taglist.has_key_value("foot", "customers")) {
						writer.writeWay(L_WP, way, "default", "foot=%s on highway=%s is broken - road is public", footvalue, highway);
					} else if (taglist.has_key_value("foot", "destination")) {
						writer.writeWay(L_WP, way, "default", "foot=%s on highway=%s is broken - No way StVO can sign this", footvalue, highway);
					}
				}

				if (taglist.key_value_in_list("highway", { "track", "service" })) {
					if (taglist.key_value_is_true("foot")) {
						writer.writeWay(L_DEFAULTS, way, "redundant", "foot=%s on highway=%s is default", footvalue, highway);
					}
				}

				if (taglist.key_value_in_list("highway", { "trunk", "trunk_link", "motorway", "motorway_link" })) {
					if (taglist.key_value_is_true("foot")) {
						writer.writeWay(L_WP, way, "default", "foot=%s on highway=%s is broken", footvalue, highway);
					}
				}

				if (!taglist.key_value_in_list("foot", { "yes", "no", "private", "permissive", "destination" , "designated", "use_sidepath" })) {
					writer.writeWay(L_STRANGE, way, "default", "foot=%s on highway=%s", footvalue, highway);
				}
			}
		}

		void tag_motor_vehicle(osmium::Way& way, extendedTagList& taglist) {
			if (taglist.key_value_is_true("motor_vehicle")) {
				if (taglist.key_value_is_false("motorcycle")) {
					writer.writeWay(L_WP, way, "default", "motor_vehicle=yes and motorcycle=no should be motorcar + hgv");
				} else if (taglist.key_value_is_true("motorcycle")) {
					writer.writeWay(L_DEFAULTS, way, "redundant", "motor_vehicle=yes includes motorcycle=yes");
				}

				if (taglist.key_value_is_false("motorcar")) {
					writer.writeWay(L_WP, way, "default", "motor_vehicle=yes and motorcar=no should be motorcycle");
				} else if (taglist.key_value_is_true("motorcar")) {
					writer.writeWay(L_DEFAULTS, way, "redundant", "motor_vehicle=yes includes motorcar=yes");
				}

				if (taglist.key_value_is_false("hgv")) {
					writer.writeWay(L_WP, way, "default", "motor_vehicle=yes and hgv=no should be motorcar");
				} else if (taglist.key_value_is_true("hgv")) {
					writer.writeWay(L_DEFAULTS, way, "redundant", "motor_vehicle=yes includes hgv=yes");
				}
			}
		}

		void tag_access(osmium::Way& way, extendedTagList& taglist) {
			if (taglist.has_key("access")) {
				if (taglist.key_value_is_true("access")) {
					writer.writeWay(L_DEFAULTS, way, "violetline", "access=yes is default");
				} else {
					if (taglist.road_is_public()) {
						const char *accessvalue=taglist.get_value_by_key("access");
						writer.writeWay(L_WP, way, "violetline", "access=%s - Nicht StVO konform. Vermutlich motor_vehicle=%s oder vehicle=%s",
								accessvalue, accessvalue, accessvalue);
					}
				}
			}
		}

		// footway=* is only defined for highway=footway
		//
		// Old values (both, left, right, none) are deprecated
		//
		void tag_footway(osmium::Way& way, extendedTagList& taglist) {
			if (!taglist.has_key("footway"))
				return;

			if (taglist.key_value_in_list("footway", { "both", "left", "right", "none" })) {
				writer.writeWay(L_WP, way, "default", "footway=%s on highway=%s is deprecated - replaced by sidewalk=",
					taglist.get_value_by_key("footway"), taglist.get_value_by_key("highway"));
			} else {
				if (!taglist.has_key_value("highway", "footway")) {
					writer.writeWay(L_WP, way, "default", "footway=%s on non highway=footway",
						taglist.get_value_by_key("footway"));
				} else {
					if (!taglist.key_value_in_list("footway", { "sidewalk", "crossing" })) {
						writer.writeWay(L_WP, way, "default", "footway=%s is unknown value",
							taglist.get_value_by_key("footway"));
					}
				}
			}
		}

		bool is_bridge(extendedTagList& taglist) {
			return taglist.key_value_in_list("bridge", { "yes", "true", "1" });
		}

		bool is_tunnel(extendedTagList& taglist) {
			return taglist.key_value_in_list("tunnel", { "yes", "true", "1", "avalanche_protector", "building_passage" });
		}

		void tag_overtaking(osmium::Way& way, extendedTagList& taglist) {
				std::vector<const char *>	keys={ "overtaking", "overtaking:forward", "overtaking:backward" };

				for(auto key : keys) {
					if (!taglist.has_key(key))
						continue;
					if (!taglist.key_value_in_list(key, { "no", "yes", "caution", "both", "forward", "backward" })) {
						writer.writeWay(L_WP, way, "default", "%s=%s value not in known list",
							key, taglist[key]);
					}
					// TODO - lanes=1 - no overtaking
					//        lanes:forward/lanes:backward/oneway?
					// TODO - overtaking:hgv, overtaking:forward:hgv
				}

				if (taglist.key_value_in_list("overtaking:forward", { "both", "backward" })) {
					writer.writeWay(L_WP, way, "default", "overtaking:forward=%s is broken",
						taglist["overtaking:forward"]);
					// TODO - oneway in opposite direction
				}

				if (taglist.key_value_in_list("overtaking:backward", { "both", "forward" })) {
					writer.writeWay(L_WP, way, "default", "overtaking:backward=%s is broken",
						taglist["overtaking:backward"]);
					// TODO - oneway in opposite direction
				}
		}

		void tag_cutting(osmium::Way& way, extendedTagList& taglist) {
			if (!taglist.has_key("cutting"))
				return;

			if (!taglist.key_value_in_list("cutting", { "no", "yes", "1", "0", "true", "false", "left", "right" })) {
				writer.writeWay(L_WP, way, "default", "cutting=%s is not in known value list",
					taglist["cutting"]);
			}

			if (taglist.key_value_in_list("cutting", { "yes", "1", "true", "left", "right" })) {
				if (is_tunnel(taglist)) {
					writer.writeWay(L_WP, way, "default", "cutting=%s and tunnel=%s is broken",
						taglist["cutting"], taglist["tunnel"]);
				}
				if (is_bridge(taglist)) {
					writer.writeWay(L_WP, way, "default", "cutting=%s and bridge=%s is broken",
						taglist["cutting"], taglist["bridge"]);
				}
			} else if (taglist.key_value_in_list("cutting", { "no", "0", "false" })) {
					writer.writeWay(L_DEFAULTS, way, "default", "cutting=no is default");
			}
		}

		void tag_embankment(osmium::Way& way, extendedTagList& taglist) {
			if (!taglist.has_key("embankment"))
				return;

			if (!taglist.key_value_in_list("embankment", { "no", "yes", "1", "0", "true", "false" })) {
				writer.writeWay(L_WP, way, "default", "embankment=%s is not in known value list",
					taglist["embankment"]);
			}

			if (taglist.key_value_in_list("embankment", { "yes", "1", "true" })) {
				if (is_tunnel(taglist)) {
					writer.writeWay(L_WP, way, "default", "embankment=%s and tunnel=%s is broken",
						taglist["embankment"], taglist["tunnel"]);
				}
				if (is_bridge(taglist)) {
					writer.writeWay(L_WP, way, "default", "embankment=%s and bridge=%s is broken",
						taglist["embankment"], taglist["bridge"]);
				}
				if (taglist.key_value_in_list("cutting", { "yes", "1", "true" })) {
					writer.writeWay(L_WP, way, "default", "embankment=%s and cutting=%s is broken",
						taglist["embankment"], taglist["cutting"]);
				}
			} else if (taglist.key_value_in_list("embankment", { "no", "0", "false" })) {
					writer.writeWay(L_DEFAULTS, way, "default", "embankment=no is default");
			}
		}

		void tag_lit(osmium::Way& way, extendedTagList& taglist) {
			if (!taglist.has_key("lit"))
				return;

			if (!taglist.key_value_in_list("lit", { "no", "yes", "limited", "24/7", "automatic" })) {
				writer.writeWay(L_WP, way, "default", "lit=%s is not in known value list",
					taglist.get_value_by_key("lit"));
			}

			if (taglist.key_value_in_list("lit", { "yes", "limited", "24/7", "automatic" })
				&& taglist.key_value_in_list("highway", { "track" })) {

				writer.writeWay(L_STRANGE, way, "default", "lit=%s on highway=%s is strange",
					taglist.get_value_by_key("lit"), taglist.get_value_by_key("highway"));
			}
		}

		void tag_hazmat(osmium::Way& way, extendedTagList& taglist) {
			if (!taglist.has_key("hazmat"))
				return;

			if (!taglist.key_value_in_list("hazmat", { "no", "yes", "destination", "designated" })) {
				writer.writeWay(L_WP, way, "default", "hazmat=%s is not in known value list",
					taglist.get_value_by_key("hazmat"));
			}

			if (taglist.key_value_in_list("hazmat", { "yes", "destination", "designated" })) {

				// Ways not beeing part of the "Gefahrgutstraßengrundnetz"
				if (taglist.key_value_in_list("highway", { "track", "path", "footway", "cycleway", "pedestrian" })) {
					writer.writeWay(L_WP, way, "default", "hazmat=%s on highway=%s is broken",
						taglist.get_value_by_key("hazmat"), taglist.get_value_by_key("highway"));

				// Ways most likely not beeing part of the "Gefahrgutstraßengrundnetz"
				} else if (taglist.key_value_in_list("highway", { "living_street", "service" })) {
					writer.writeWay(L_WP, way, "default", "hazmat=%s on highway=%s is suspicious",
						taglist.get_value_by_key("hazmat"), taglist.get_value_by_key("highway"));
				}

				// hazmat allowed but not goods vehicles
				if (taglist.key_value_in_list("hgv", { "no", "false", "0" })) {
					writer.writeWay(L_WP, way, "default", "hazmat=%s with hgv=%s is suspicious",
						taglist.get_value_by_key("hazmat"), taglist.get_value_by_key("hgv"));
				}
			}
		}

		void tag_goods(osmium::Way& way, extendedTagList& taglist) {
			if (taglist.has_key("goods")) {
				writer.writeWay(L_WP, way, "default", "goods=* is not in use in Germany - did you mean hgv=");
			}
		}

		void tag_stray(osmium::Way& way, extendedTagList& taglist) {
			if (taglist.has_key("entrance")) {
				writer.writeWay(L_WP, way, "default", "entrance=* is not used on highways but on nodes");
			}
			if (taglist.has_key("waterway")) {
				writer.writeWay(L_WP, way, "default", "waterway=%s is incompatible with a street",
						taglist.get_value_by_key("waterway"));
			}
			if (taglist.has_key("building")) {
				writer.writeWay(L_WP, way, "default", "building=%s is incompatible with a street",
						taglist.get_value_by_key("building"));
			}
		}

		void tag_cycleway(osmium::Way& way, extendedTagList& taglist) {
			/* TODO
			 * On non cycleway/paths cycleway may only be no/left/right/both
			 */

			if (taglist.key_value_in_list("cycleway:left", { "none", "no", "0" }) &&
				taglist.key_value_in_list("cycleway:right", { "none", "no", "0" })) {

				writer.writeWay(L_CYCLING, way, "default", "cycleway:left + cycleway:right are the same - should be cycleway=no");
			}

			bool	left=false,
				right=false;

			/* If we have cycleway:left/cycleway:right we need to have cycleway aswell */
			if (taglist.has_key("cycleway:left")
				&& !taglist.key_value_in_list("cycleway:left", { "none", "no", "0" })) {
				left=true;
			}
			if (taglist.has_key("cycleway:right")
				&& !taglist.key_value_in_list("cycleway:right", { "none", "no", "0" })) {
				right=true;
			}

			if (left || right) {
				if (!taglist.has_key("cycleway")) {
					writer.writeWay(L_CYCLING, way, "default", "way has cycleway:left/right=* and no cycleway=*");
				}
				if (left && !right) {
					if (!taglist.has_key_value("cycleway", "left")) {
						writer.writeWay(L_CYCLING, way, "default", "way has cycleway:left=* and no cycleway=left");
					}
				} else if (!left && right) {
					if (!taglist.has_key_value("cycleway", "right")) {
						writer.writeWay(L_CYCLING, way, "default", "way has cycleway:right=* and no cycleway=right");
					}
				} else if (left && right) {
					if (!taglist.has_key_value("cycleway", "both")) {
						writer.writeWay(L_CYCLING, way, "default", "way has cycleway:right=* and left=* and no cycleway=both");
					}
				}
			}

			std::vector<const char *>	cycleways={ "cycleway:left ", "cycleway:right" };
			for(auto cw : cycleways) {
				if (taglist.has_key(cw) && !taglist.key_value_in_list(cw, { "sidepath", "track", "lane" })) {
					writer.writeWay(L_CYCLING, way, "default", "%s=%s invalid combination",
							cw, taglist.get_value_by_key(cw));
				}
			}

			// TODO - Other values which might be the same
		}

		void tag_vehicle(osmium::Way& way, extendedTagList& taglist) {
			if (taglist.key_value_is_true("vehicle")) {
				if (taglist.key_value_is_false("motor_vehicle")) {
					writer.writeWay(L_WP, way, "default", "vehicle=yes and motor_vehicle=no should be bicyle");
				} else if (taglist.key_value_is_true("motor_vehicle")) {
					writer.writeWay(L_DEFAULTS, way, "redundant", "vehicle=yes includes motor_vehicle=yes");
				}
			}
		}

		void highway_road(osmium::Way& way, extendedTagList& taglist) {
			if (taglist.has_key_value("highway", "road")) {
				writer.writeWay(L_WP, way, "default", "highway=road is only a temporary tagging for sat imagery based mapping");
			}
		}

		void highway_footway(osmium::Way& way, extendedTagList& taglist) {
			if (!taglist.has_key_value("highway", "footway"))
				return;

			if (!taglist.has_key("bicycle")) {
				writer.writeWay(L_FOOTWAY, way, "footway", "highway=footway without bicycle=yes/no tag - suspicious combination");
			}

			if (taglist.has_key_value("bicycle", "use_sidepath")) {
				writer.writeWay(L_CYCLING, way, "default", "bicycle=use_sidepath on cycleway is broken - should be on main road");
			}

			if (taglist.key_value_is_true("foot")) {
				writer.writeWay(L_DEFAULTS, way, "redundant", "highway=footway with foot=yes is default");
				writer.writeWay(L_FOOTWAY, way, "redundant", "highway=footway with foot=yes is default");
			} else if (taglist.key_value_is_false("foot")) {
				writer.writeWay(L_WP, way, "default", "highway=footway with foot=no is broken");
				writer.writeWay(L_FOOTWAY, way, "default", "highway=footway with foot=no is broken");
			}

			/* TODO What about other access types vehicle/motor_vehicle/hgv/goods? yes/designated etc */
		}

		void highway_path(osmium::Way& way, extendedTagList& taglist) {
			if (taglist.has_key_value("highway", "path")) {
				if (taglist.has_key("cycleway")) {
					if (taglist.key_value_in_list("cycleway", { "shared", "track" })) {
						writer.writeWay(L_WP, way, "default", "highway=path with cycleway=%s tag should be on road or absent",
								taglist.get_value_by_key("cycleway"));
					} else {
						writer.writeWay(L_WP, way, "default", "highway=path with cycleway=%s is unknown value",
								taglist.get_value_by_key("cycleway"));
					}
				}
			}

			if (taglist.has_key_value("highway", "path")) {
				std::vector<const char *>	multitrack={ "motorcar", "goods", "hgv", "psv", "motor_vehicle", "agricultural", "atv", "bus" };
				for(auto key : multitrack) {
					if (taglist.key_value_is_true(key)) {
						writer.writeWay(L_WP, way, "default", "highway=path - %s=yes is suspicious - cant fit on single track path", key);
					} else if (taglist.key_value_is_false(key)) {
						writer.writeWay(L_DEFAULTS, way, "redundant", "highway=path - %s=no is default", key);
					} else if (taglist.has_key_value(key, "permissive")) {
						writer.writeWay(L_WP, way, "default", "highway=path - %s=permissive - cant fit on a single track path", key);
					} else if (taglist.has_key_value(key, "private")) {
						writer.writeWay(L_WP, way, "default", "highway=path - %s=private - cant fit on a single track path", key);
					} else if (taglist.has_key_value(key, "agricultural")) {
						writer.writeWay(L_WP, way, "default", "highway=path - %s=agricultural - cant fit on a single track path", key);
					}
				}
				/* Broken tags - hazmat=no/yes bullshit */
			}
		}

		void highway_service(osmium::Way& way, extendedTagList& taglist) {
			if (taglist.has_key_value("highway", "service")) {
				if (taglist.has_key("name")) {
					writer.writeWay(L_WP, way, "default", "highway=service with name=* is suspicious - Either public e.g. not service or name tag abuse");
				}
			} else {
				if (taglist.has_key("service")) {
					writer.writeWay(L_WP, way, "default", "service=%s on non service highway", taglist.get_value_by_key("service"));
				}
			}
		}

		void highway_living_street(osmium::Way& way, extendedTagList& taglist) {
			if (taglist.has_key_value("highway", "living_street")) {
				if (taglist.has_key("maxspeed")) {
					writer.writeWay(L_WP, way, "steelline", "maxspeed=%s on living_street is broken - neither numeric nor walk is correct",
							taglist.get_value_by_key("maxspeed"));
					// TODO - maxspeed:vehicle, maxspeed:motor_vehicle, maxspeed:hgv, maxspeed:motorcar, maxspeed:motorcycle etc
				}

				if (taglist.has_key_value("bicycle", "use_sidepath")) {
					writer.writeWay(L_CYCLING, way, "default", "bicycle=use_sidepath on living_street is broken - living_street explicitly includes bicycles");
				}

				std::vector<const char *>	defaultyes={ "vehicle" };
				for(auto key : defaultyes) {
					if (taglist.key_value_is_false(key)) {
						writer.writeWay(L_WP, way, "default", "living_street with %s=no is broken", key);
					} else if (taglist.key_value_is_true(key)) {
						writer.writeWay(L_DEFAULTS, way, "redundant", "living_street with %s=yes is default", key);
					}
				}
			}
		}

		void highway_track(osmium::Way& way, extendedTagList& taglist) {
			if (taglist.has_key_value("highway", "track")) {
				if (taglist.has_key("name")) {
					writer.writeWay(L_WP, way, "brownline", "highway=track with name is suspicious - probably not track");
				}
				if (taglist.has_key("maxspeed")) {
					writer.writeWay(L_WP, way, "steelline", "highway=track with maxspeed is suspicious - probably not track");
				}

				std::vector<const char *>	defaultno={ "motorcycle", "motorcar", "hgv", "psv", "motor_vehicle", "vehicle" };
				for(auto key : defaultno) {
					if (taglist.key_value_is_false(key)) {
						writer.writeWay(L_WP, way, "brownline", "highway=track - %s=no is suspicious - should be agricutural or empty", key);
					}
				}
			}
		}

		void highway_cycleway(osmium::Way& way, extendedTagList& taglist) {
			if (taglist.has_key_value("highway", "cycleway")) {

				std::vector<const char *>	defno={ "motor_vehicle", "motorcar", "motorcycle", "hgv", "psv", "horse", "foot" };
				for(auto key : defno) {
					if (taglist.key_value_is_false(key)) {
						writer.writeWay(L_CYCLING, way, "redundant", "%s=%s on cycleway is default",
								key, taglist.get_value_by_key(key));
					}
				}

				if (taglist.has_key_value("vehicle", "no")) {
					writer.writeWay(L_CYCLING, way, "default", "vehicle=no on cycleway is broken as bicycle is a vehicle");
				}

				/*
				 * FIXME - Luebecker Modell sieht das vor.
				if (taglist.key_value_in_list("bicycle", { "designated" })) {
					writer.writeWay(L_CYCLING, way, "redundant", "bicycle=%s on cycleway is default",
							taglist.get_value_by_key("bicycle"));
				}
				*/

				if (taglist.key_value_in_list("bicycle", { "no", "0", "false", "private", "permissive",
						"use_sidepath", "destination", "customers", "unknown", "lane",
						"allowed", "limited",  })) {
					writer.writeWay(L_CYCLING, way, "default", "bicycle=%s on cycleway is broken",
							taglist.get_value_by_key("bicycle"));
				}

				if (taglist.has_key_value("bicycle", "use_sidepath")) {
					writer.writeWay(L_CYCLING, way, "default", "cycleway=track and bicycle=use_sidepath on road is broken as there is no seperate cycleway");
				}
			}
		}

		bool highway_wecare(extendedTagList& taglist) {
			if (!taglist.has_key("highway")) {
				return false;
			}

			const std::vector<std::string> highway_valid {
					"motorway", "motorway_link",
					"trunk", "trunk_link",
					"primary", "primary_link",
					"secondary", "secondary_link",
					"tertiary", "tertiary_link",
					"unclassified", "residential",
					"living_street",
					"footway", "cycleway", "path", "bridleway",
					"service", "track",
					"road", "pedestrian", "steps", "construction"
					};

			if (!taglist.key_value_in_list("highway", highway_valid)) {
				//std::cerr << "Unknown highway type " << taglist.get_value_by_key("highway") << std::endl;
				return false;
			}

			return true;
		}

		void way(osmium::Way& way) {
			extendedTagList	taglist(way.tags());

			/* Skip highway=bus_stop */
			if (!highway_wecare(taglist))
				return;

			circular_way(way, taglist);

			tag_layer(way, taglist);
			tag_ref(way, taglist);
			tag_maxspeed(way, taglist);		// maxspeed:forward, maxspeed:backward
			tag_maxheight(way, taglist);
			tag_lanes(way, taglist);	// turn:lanes, destination:lanes
			tag_sidewalk(way, taglist);
			tag_segregated(way, taglist);
			tag_shoulder(way, taglist);
			tag_oneway(way, taglist);
			tag_construction(way, taglist);
			tag_proposed(way, taglist);
			tag_tracktype(way, taglist);
			tag_tunnel(way, taglist);
			tag_junction(way, taglist);
			tag_footway(way, taglist);
			tag_hazmat(way, taglist);
			tag_lit(way, taglist);
			tag_embankment(way, taglist);
			tag_cutting(way, taglist);
			tag_overtaking(way, taglist);
			tag_maxwidth(way, taglist);
			tag_type(way, taglist);

			tag_source_maxspeed(way, taglist);
			tag_maxspeed_source(way, taglist);
			tag_maxspeed_type(way, taglist);

			node_only_tags(way, taglist);

			// TODO - surface
			// TODO - smoothness
			// TODO - incline
			// TODO - trafic_calming (on ways)
			// TODO - driving_side
			// TODO - abutters
			// TODO - maxspeed:conditional
			// TODO - maxspeed:source
			// TODO - cycleway, cycleway:right, cycleway:left
			// TODO - parking:lane
			// TODO - lanes:both_ways!??
			// TODO - maxaxleload
			// TODO - maxlength
			// TODO - maxwidth
			// TODO - maxwidth:physical
			// TODO - covered

			tag_bicycle(way, taglist);
			tag_foot(way, taglist);
			tag_access(way, taglist);
			tag_goods(way, taglist);
			tag_motor_vehicle(way, taglist);
			tag_vehicle(way, taglist);
			tag_cycleway(way, taglist);

			tag_stray(way, taglist);
			// TODO - psv
			// TODO - motorcycle
			// TODO - hgv
			// TODO - forestry
			// TODO - agricultural
			// TODO - wheelchair

			highway_road(way, taglist);
			highway_footway(way, taglist);
			highway_cycleway(way, taglist);
			highway_path(way, taglist);
			highway_living_street(way, taglist);
			highway_service(way, taglist);
			highway_track(way, taglist);

			// steps
			// escalators
			// pedestrian

			if (taglist.road_is_public()) {
				const std::vector<const char *>	accesstags={
					"access", "vehicle", "motor_vehicle", "motorcycle",
					"motorcar", "hgv", "psv",
					"goods", "mofa", "moped", "horse"};

				for(auto key : accesstags) {
					const char *value=taglist[key];
					const char *highway=taglist["highway"];

					if (!value)
						continue;

					if (!strcmp(value, "permissive")) {
						writer.writeWay(L_WP, way, "violetline", "highway=%s is public way - cant have %s=permissive access tags", highway, key);
					} else if (!strcmp(value, "private")) {
						writer.writeWay(L_WP, way, "violetline", "highway=%s is public way - cant have %s=private access tags", highway, key);
					} else if (!strcmp(value, "customers")) {
						writer.writeWay(L_WP, way, "violetline", "highway=%s is public way - cant have %s=customers access tags", highway, key);
					}
				}
			}
		}
};


namespace po = boost::program_options;

int main(int argc, char* argv[]) {

	po::options_description         desc("Allowed options");
        desc.add_options()
                ("help,h", "produce help message")
                ("infile,i", po::value<std::string>()->required(), "Input file")
		("dbname,d", po::value<std::string>()->required(), "Output database name")
        ;
        po::variables_map vm;
	try {
		po::store(po::parse_command_line(argc, argv, desc), vm);
		po::notify(vm);
	} catch(boost::program_options::required_option& e) {
		std::cerr << "Error: " << e.what() << "\n";
		exit(-1);
	}

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
}

