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
	L_DEFAULT,
	L_TRACK,
	L_STRANGE,
	layermax
};

class SpatiaLiteWriter : public osmium::handler::Handler {
	std::array<gdalcpp::Layer *, layermax>	layer;

	gdalcpp::Dataset		dataset;
	osmium::geom::OGRFactory<>	m_factory{};

	public:

	explicit SpatiaLiteWriter(std::string &dbname) :
			dataset("sqlite", dbname, gdalcpp::SRS{}, {  "SPATIALITE=TRUE", "INIT_WITH_EPSG=no" }) {

			dataset.exec("PRAGMA synchronous = OFF");

			layer[L_WP]=addLineStringLayer("wayproblems");
			layer[L_REF]=addLineStringLayer("ref");
			layer[L_FOOTWAY]=addLineStringLayer("footway");
			layer[L_STRANGE]=addLineStringLayer("strange");
			layer[L_TRACK]=addLineStringLayer("track");
			layer[L_DEFAULT]=addLineStringLayer("default");
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

			feature.add_to_layer();

			std::cout << " way " << way.id() << " "
				<< "changeset " << way.changeset() << " "
				<< way.user() << " "
				<< way.timestamp().to_iso() << " - "
				<< problem << " "
				<< std::endl;

		} catch (gdalcpp::gdal_error) {
			std::cerr << "gdal_error while creating feature wayid " << way.id()<< std::endl;
		} catch (osmium::invalid_location) {
			std::cerr << "invalid location wayid " << way.id() << std::endl;
		} catch (osmium::geometry_error) {
			std::cerr << "geometry error wayid " << way.id() << std::endl;
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
			} catch(std::invalid_argument) {
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
			} catch(std::invalid_argument) {
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

		bool is_motor_vehicle_road() {
			const char *highway=get_value_by_key("highway");
			return string_in_list(highway, motor_vehicle_highway);
		}

		bool is_public_road() {
			return string_in_list(get_value_by_key("highway"), highway_public);
		}
};


class WayHandler : public osmium::handler::Handler {
	SpatiaLiteWriter	&writer;

	public:
		WayHandler(SpatiaLiteWriter &writer) : writer(writer) {};

		void way(osmium::Way& way) {
			extendedTagList	taglist(way.tags());
			const char *highway=taglist.get_value_by_key("highway");

			if (!taglist.has_key("highway")) {
				return;
			}

			if (way.ends_have_same_id()) {
				if (!taglist.has_key_value("area", "yes")
					&& !taglist.has_key_value("junction", "roundabout")) {
					writer.writeWay(L_STRANGE, way, "default", "Circular way without junction=roundabout");
				}
			}

			if (taglist.highway_should_have_ref()) {
				if (!taglist.has_key_value("junction", "roundabout")) {
					if (!taglist.has_key("ref")) {
						writer.writeWay(L_REF, way, "ref", "highway should have ref");
					}
				}
			}

			if (taglist.has_key("layer")) {
				if (!taglist.key_value_is_int("layer")) {
					writer.writeWay(L_WP, way, "default", "layer=%s is not integer", taglist.get_value_by_key("layer"));
				} else {
					int layer=taglist.key_value_as_int("layer");
					if (layer == 0) {
						writer.writeWay(L_DEFAULT, way, "redundant", "layer=%s is default", taglist.get_value_by_key("layer"));
					} else if (layer > 10) {
						writer.writeWay(L_WP, way, "redundant", "layer=%s where num > 10 seems broken", taglist.get_value_by_key("layer"));
					} else if (layer < -10) {
						writer.writeWay(L_WP, way, "redundant", "layer=%s where num < -10 seems broken", taglist.get_value_by_key("layer"));
					}
				}
			}

			if (taglist.has_key("sidewalk")) {
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
			}

			if (taglist.has_key("segregated")) {
				if (!taglist.key_value_in_list("highway", { "footway", "cycleway", "path" })) {
					writer.writeWay(L_WP, way, "default", "highway=%s and segregated=%s - segregated only used on foot/cycleway and path",
						taglist.get_value_by_key("highway"),taglist.get_value_by_key("segregated"));
				}
				if (!taglist.key_value_in_list("segregated", { "yes", "no" })) {
					writer.writeWay(L_WP, way, "default", "segregated=%s - value not in known value list",
						taglist.get_value_by_key("segregated"));
				}
			}


			if (taglist.has_key("bicycle")) {
				const char *bikevalue=taglist.get_value_by_key("bicycle");

				if (taglist.key_value_in_list("highway", { "living_street",
						"residential", "unclassified",
						"tertiary", "tertiary_link",
						"secondary", "secondary_link",
						"primary", "primary_link" })) {

					if (taglist.key_value_is_true("bicycle")) {
						writer.writeWay(L_DEFAULT, way, "redundant", "bicycle=%s on highway=%s is default", bikevalue, highway);
					} else if (taglist.has_key_value("bicycle", "permissive")) {
						writer.writeWay(L_DEFAULT, way, "redundant", "bicycle=designated on highway=%s is default - road is public", highway);
					} else if (taglist.has_key_value("bicycle", "private")) {
						writer.writeWay(L_WP, way, "default", "bicycle=%s on highway=%s is broken - road is public", bikevalue, highway);
					} else if (taglist.has_key_value("bicycle", "customers")) {
						writer.writeWay(L_WP, way, "default", "bicycle=%s on highway=%s is broken - road is public", bikevalue, highway);
					} else if (taglist.has_key_value("bicycle", "destination")) {
						writer.writeWay(L_WP, way, "default", "bicycle=%s on highway=%s is suspicious - StVO would allow vehicle=destination", bikevalue, highway);
					}
				}

				if (taglist.key_value_in_list("highway", { "track", "service" })) {
					if (taglist.key_value_is_true("bicycle")) {
						writer.writeWay(L_DEFAULT, way, "redundant", "bicycle=%s on highway=%s is redundant", bikevalue, highway);
					}
				}

				if (taglist.key_value_in_list("highway", { "trunk", "trunk_link", "motorway", "motorway_link" })) {
					if (taglist.key_value_in_list("bicycle", { "no", "0", "false" })) {
						writer.writeWay(L_DEFAULT, way, "redundant", "bicycle=%s on highway=%s is default", bikevalue, highway);
					} else {
						writer.writeWay(L_WP, way, "default", "bicycle=%s on highway=%s is broken", bikevalue, highway);
					}
				}

				if (!taglist.key_value_in_list("bicycle", { "yes", "no", "private", "permissive",
						"destination" , "designated", "use_sidepath", "dismount" })) {
					writer.writeWay(L_STRANGE, way, "default", "bicycle=%s on highway=%s", bikevalue, highway);
				}
			}


			if (taglist.has_key("foot")) {
				const char *footvalue=taglist.get_value_by_key("foot");

				if (taglist.key_value_in_list("highway", { "living_street",
						"residential", "unclassified",
						"tertiary", "tertiary_link",
						"secondary", "secondary_link",
						"primary", "primary_link" })) {

					if (taglist.key_value_is_true("foot")) {
						writer.writeWay(L_DEFAULT, way, "redundant", "foot=%s on highway=%s is default", footvalue, highway);
					} else if (taglist.has_key_value("foot", "permissive")) {
						writer.writeWay(L_WP, way, "default", "foot=yes on highway=%s is default - permissive on public road is broken", highway);
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
						writer.writeWay(L_DEFAULT, way, "redundant", "foot=%s on highway=%s is default", footvalue, highway);
					}
				}

				if (taglist.key_value_in_list("highway", { "trunk", "trunk_link", "motorway", "motorway_link" })) {
					if (taglist.key_value_is_true("foot")) {
						writer.writeWay(L_WP, way, "default", "foot=%s on highway=%s is broken", footvalue, highway);
					}
				}

				if (!taglist.key_value_in_list("foot", { "yes", "no", "private", "permissive", "destination" , "designated" })) {
					writer.writeWay(L_STRANGE, way, "default", "foot=%s on highway=%s", footvalue, highway);
				}
			}

			if (taglist.has_key("shoulder")) {
				if (!taglist.key_value_in_list("shoulder", { "both", "left", "right", "no", "yes" })) {
					writer.writeWay(L_WP, way, "default", "shoulder=%s not in known value list", taglist.get_value_by_key("shoulder"));
				}
			}

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

			/*
			 * Maxspeed
			 *
			 */

			const std::vector<const char *>	maxspeedtags={ "maxspeed", "maxspeed:forward", "maxspeed:backward" };
			for(auto key : maxspeedtags) {
				if (taglist.has_key(key)) {
					if (!taglist.key_value_in_list(key, { "none", "signals" })) {
						try {
							int ms=std::stoi(taglist.get_value_by_key(key), nullptr, 10);
						} catch(std::invalid_argument) {
							writer.writeWay(L_WP, way, "default", "%s=%s is not numerical",
									key, taglist.get_value_by_key(key));
						}
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
				writer.writeWay(L_WP, way, "default", "maxspeed and maxspeed:forward/backward - overlapping values");
			}

			/*
			 * Maxheight
			 */
			if (taglist.has_key("maxheight")) {
				if (!taglist.key_value_in_list("maxheight", { "default", "none", "unsigned", "no_sign", "no_indications", "below_default" })) {
					if (!taglist.key_value_is_double("maxheight")) {
						writer.writeWay(L_WP, way, "default", "maxheight=%s is not float",
							taglist.get_value_by_key("maxheight"));
					} else {
						double maxheight=taglist.key_value_as_double("maxheight");
						if (maxheight < 1.8) {
							// https://www.openstreetmap.org/way/25048948
							writer.writeWay(L_WP, way, "default", "maxheight=%s is less than 1.8",
								taglist.get_value_by_key("maxheight"));
							// TODO - Maxheight for general traffic - parking access might be lower
						} else if (maxheight > 7) {
							// https://www.openstreetmap.org/way/25363727
							writer.writeWay(L_WP, way, "default", "maxheight=%s is more than 7 - suspicous value",
								taglist.get_value_by_key("maxheight"));
						}
					}
				}
			}


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
						writer.writeWay(L_WP, way, "default", "%s=%s makes only sense on oneway streets",
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

			if (!taglist.highway_may_have_ref()) {
				if (!taglist.has_key_value("highway", "path")) {
					if (taglist.has_key("ref")) {
						writer.writeWay(L_REF, way, "ref", "highway should not have ref");
					}
				}
			}

			if (taglist.has_key_value("highway", "road")) {
				writer.writeWay(L_WP, way, "default", "highway=road is only a temporary tagging for sat imagery based mapping");
			}

			if (taglist.is_public_road()) {
				const std::vector<const char *>	accesstags={
					"access", "vehicle", "motor_vehicle", "motorcycle",
					"motorcar", "hgv", "psv",
					"goods", "mofa", "moped", "horse"};

				for(auto key : accesstags) {
					const char *value=taglist.get_value_by_key(key);
					const char *highway=taglist.get_value_by_key("highway");

					if (!value)
						continue;

					if (!strcmp(value, "permissive")) {
						writer.writeWay(L_WP, way, "default", "highway=%s is public way - cant have %s=permissive access tags", highway, key);
					} else if (!strcmp(value, "private")) {
						writer.writeWay(L_WP, way, "default", "highway=%s is public way - cant have %s=private access tags", highway, key);
					} else if (!strcmp(value, "customers")) {
						writer.writeWay(L_WP, way, "default", "highway=%s is public way - cant have %s=customers access tags", highway, key);
					}
				}


				if (taglist.has_key_value("cycleway", "track") && taglist.has_key_value("bicycle", "use_sidepath")) {
					writer.writeWay(L_WP, way, "default", "cycleway=track and bicycle=use_sidepath on road is broken as there is no seperate cycleway");
				}
			}

			if (taglist.has_key("goods")) {
				writer.writeWay(L_WP, way, "default", "goods=* is not in use in Germany - no distinction between goods and hgv");
			}

			if (taglist.has_key("access")) {
				if (taglist.key_value_is_false("access")) {
					writer.writeWay(L_WP, way, "default", "access=no - Nicht StVO konform. Vermutlich motor_vehicle=no oder vehicle=no");
				} else if (taglist.key_value_is_true("access")) {
					writer.writeWay(L_DEFAULT, way, "redundant", "access=yes is default");
				} else if (taglist.has_key_value("access", "destination")) {
					writer.writeWay(L_WP, way, "default", "access=destination nicht StVO konform. Vermutlich vehicle=destination oder motor_vehicle=destination");
				}
			}

			if (taglist.has_key_value("highway", "service")) {
				if (taglist.has_key("name")) {
					writer.writeWay(L_WP, way, "default", "highway=service with name=* is suspicious - Either public e.g. not service or name tag abuse");
				}
			} else {
				if (taglist.has_key("service")) {
					writer.writeWay(L_WP, way, "default", "service=%s on non service highway", taglist.get_value_by_key("service"));
				}
			}

			if (taglist.has_key_value("highway", "track")) {
				if (taglist.has_key("name")) {
					writer.writeWay(L_TRACK, way, "default", "highway=track with name is suspicious - probably not track");
				}
				if (taglist.has_key("maxspeed")) {
					writer.writeWay(L_TRACK, way, "default", "highway=track with maxspeed is suspicious - probably not track");
				}

				std::vector<const char *>	defaultno={ "motorcycle", "motorcar", "hgv", "psv", "motor_vehicle", "vehicle" };
				for(auto key : defaultno) {
					if (taglist.key_value_is_false(key)) {
						writer.writeWay(L_TRACK, way, "default", "highway=track - %s=no is suspicious - should be agricutural or empty", key);
					}
				}
			} else {
				if (taglist.has_key("tracktype"))
					writer.writeWay(L_WP, way, "default", "tracktype=* on non track");
			}

			if (taglist.has_key_value("highway", "footway")) {
				if (!taglist.has_key("bicycle")) {
					writer.writeWay(L_FOOTWAY, way, "footway", "highway=footway without bicycle=yes/no tag - suspicious combination");
				}

				if (taglist.has_key_value("bicycle", "use_sidepath")) {
					writer.writeWay(L_WP, way, "default", "bicycle=use_sidepath on cycleway is broken - should be on main road");
					writer.writeWay(L_FOOTWAY, way, "default", "bicycle=use_sidepath on cycleway is broken - should be on main road");
				}

				if (taglist.key_value_is_true("foot")) {
					writer.writeWay(L_DEFAULT, way, "redundant", "highway=footway with foot=yes is default");
					writer.writeWay(L_FOOTWAY, way, "redundant", "highway=footway with foot=yes is default");
				} else if (taglist.key_value_is_false("foot")) {
					writer.writeWay(L_WP, way, "default", "highway=footway with foot=no is broken");
					writer.writeWay(L_FOOTWAY, way, "default", "highway=footway with foot=no is broken");
				}

				/* TODO What about other access types vehicle/motor_vehicle/hgv/goods? yes/designated etc */
			}

			if (taglist.has_key_value("highway", "living_street")) {
				if (taglist.has_key("maxspeed")) {
					writer.writeWay(L_WP, way, "default", "maxspeed on living_street is broken - neither numeric nor walk is correct");
				}

				if (taglist.has_key_value("bicycle", "use_sidepath")) {
					writer.writeWay(L_WP, way, "default", "bicycle=use_sidepath on living_street is broken - living_street explicitly includes bicycles");
				}

				std::vector<const char *>	defaultyes={ "vehicle" };
				for(auto key : defaultyes) {
					if (taglist.key_value_is_false(key)) {
						writer.writeWay(L_WP, way, "default", "living_street with %s=no is broken", key);
					} else if (taglist.key_value_is_true(key)) {
						writer.writeWay(L_DEFAULT, way, "redundant", "living_street with %s=yes is default", key);
					}
				}
			}

			if (taglist.has_key_value("highway", "path")) {
				std::vector<const char *>	multitrack={ "motorcar", "goods", "hgv", "psv", "motor_vehicle", "agricultural", "atv", "bus" };
				for(auto key : multitrack) {
					if (taglist.key_value_is_true(key)) {
						writer.writeWay(L_WP, way, "default", "highway=path - %s=yes is suspicious - cant fit on single track path", key);
					} else if (taglist.key_value_is_false(key)) {
						writer.writeWay(L_WP, way, "redundant", "highway=path - %s=no is default", key);
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

			if (taglist.has_key_value("highway", "cycleway")) {
				std::vector<const char *>	defno={ "motor_vehicle", "motorcar", "motorcycle", "hgv", "psv", "horse", "foot" };
				for(auto key : defno) {
					if (taglist.key_value_is_false(key)) {
						writer.writeWay(L_DEFAULT, way, "redundant", "%s=%s on cycleway is default",
								key, taglist.get_value_by_key(key));
					}
				}
				if (taglist.has_key_value("vehicle", "no")) {
					writer.writeWay(L_WP, way, "default", "vehicle=no on cycleway is broken as bicycle is a vehicle");
				}

				if (taglist.key_value_in_list("bicycle", { "designated" })) {
					writer.writeWay(L_DEFAULT, way, "redundant", "bicycle=%s on cycleway is default",
							taglist.get_value_by_key("bicycle"));
				}

				if (taglist.key_value_in_list("bicycle", { "no", "0", "false", "private", "permissive",
						"use_sidepath", "destination", "customers", "unknown", "lane",
						"allowed", "limited",  })) {
					writer.writeWay(L_WP, way, "default", "bicycle=%s on cycleway is broken",
							taglist.get_value_by_key("bicycle"));
				}
			}

			if (taglist.key_value_is_true("vehicle")) {
				if (taglist.key_value_is_false("motor_vehicle")) {
					writer.writeWay(L_WP, way, "default", "vehicle=yes and motor_vehicle=no should be bicyle");
				} else if (taglist.key_value_is_true("motor_vehicle")) {
					writer.writeWay(L_DEFAULT, way, "redundant", "vehicle=yes includes motor_vehicle=yes");
				}
			}

			if (taglist.key_value_is_true("motor_vehicle")) {
				if (taglist.key_value_is_false("motorcycle")) {
					writer.writeWay(L_WP, way, "default", "motor_vehicle=yes and motorcycle=no should be motorcar + hgv");
				} else if (taglist.key_value_is_true("motorcycle")) {
					writer.writeWay(L_DEFAULT, way, "redundant", "motor_vehicle=yes includes motorcycle=yes");
				}

				if (taglist.key_value_is_false("motorcar")) {
					writer.writeWay(L_WP, way, "default", "motor_vehicle=yes and motorcar=no should be motorcycle");
				} else if (taglist.key_value_is_true("motorcar")) {
					writer.writeWay(L_DEFAULT, way, "redundant", "motor_vehicle=yes includes motorcar=yes");
				}

				if (taglist.key_value_is_false("hgv")) {
					writer.writeWay(L_WP, way, "default", "motor_vehicle=yes and hgv=no should be motorcar");
				} else if (taglist.key_value_is_true("hgv")) {
					writer.writeWay(L_DEFAULT, way, "redundant", "motor_vehicle=yes includes hgv=yes");
				}
			}

			if (taglist.key_value_is_false("tunnel")) {
				writer.writeWay(L_DEFAULT, way, "redundant", "tunnel=no ist default");
			}

			if (taglist.has_key("construction")) {
				if (taglist.has_key_value("construction", "yes")) {
					writer.writeWay(L_WP, way, "redundant", "construction=yes is deprecated");
				} else if (taglist.has_key_value("construction", "no")) {
					writer.writeWay(L_DEFAULT, way, "redundant", "construction=no is default");
				} else if (!taglist.key_value_in_list("construction", {
						"motorway", "motorway_link", "trunk", "trunk_link",
						"primary", "primary_link", "secondary", "secondary_link",
						"tertiary", "tertiary_link", "unclassified",
						"residential", "pedestrian", "service", "track", "cycleway", "footway",
						"steps", "minor", "path" })) {
					writer.writeWay(L_WP, way, "default", "construction=%s not in known list", taglist.get_value_by_key("construction"));
				} else {
					if (!taglist.has_key_value("highway", "construction")) {
						writer.writeWay(L_WP, way, "default", "construction=%s on highway=%s",
								taglist.get_value_by_key("highway"),
								taglist.get_value_by_key("construction"));
					}
				}
			}

			if (taglist.key_value_is_false("oneway")) {
				writer.writeWay(L_DEFAULT, way, "redundant", "oneway=no is default");
			}

			if (taglist.has_key_value("junction", "roundabout")) {
				if (taglist.has_key("name")) {
					writer.writeWay(L_WP, way, "default", "name on roundabout is most like an error");
				}
				if (taglist.has_key("ref")) {
					writer.writeWay(L_WP, way, "default", "ref on roundabout is most like an error");
				}
				if (taglist.has_key("oneway")) {
					writer.writeWay(L_DEFAULT, way, "redundant", "oneway on roundabout is default");
				}
				if (taglist.key_value_in_list("sidewalk", { "both", "yes", "left" })) {
					writer.writeWay(L_WP, way, "default", "sidewalk=%s on roundabout - Right hand drive countries should have only a right sidewalk",
							taglist.get_value_by_key("sidewalk"));
				}
				if (taglist.key_value_in_list("cycleway", { "opposite", "opposite_lane", "opposite_track" })) {
					writer.writeWay(L_WP, way, "default", "cycleway=%s on roundabout is broken",
							taglist.get_value_by_key("cycleway"));
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

