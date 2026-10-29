#include "yamlrules.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iomanip>
#include <ostream>
#include <stdexcept>

namespace {

enum class Op {
	All, Any, Not,
	HasKey, MissingKey,
	Eq, In, NotIn,
	IsTrue, IsFalse,
	IsInt, NotInt, ParsesInt, NotParsesInt,
	IsDouble, NotDouble,
	Lt, Gt, LtEq, GtEq,
	Fact,
	MapLookup, MapNeq,
	ElementsNotMatch, AnyElementNotIn,
	HasIndex, ElementEq,
	HasTurnInversion,
	SumNeq,
};

const std::string KEY_PH = "{key}";

const std::vector<std::string> TAG_TRUE{ "yes", "true", "1" };
const std::vector<std::string> TAG_FALSE{ "no", "false", "0" };

} // namespace

// Definition must live at namespace scope.
struct RuleEngine::Condition {
	Op op;
	std::string key;
	std::string value;
	std::string against;
	std::string other;
	std::string mapname;
	std::string sep;
	const std::vector<std::string> *list{};
	std::vector<std::string> values;
	std::vector<std::string> parts;
	const std::map<std::string, std::string> *map{};
	bool num_is_int{};
	long long num_i{};
	double num_d{};
	std::vector<std::unique_ptr<Condition>> children;
};

RuleEngine::RuleEngine() = default;
RuleEngine::~RuleEngine() = default;
RuleEngine::Rule::~Rule() = default;

namespace {

using Lists = std::map<std::string, std::vector<std::string>>;
using Maps = std::map<std::string, std::map<std::string, std::string>>;
using Ctx = std::map<std::string, std::string>;

bool string_in_list(const char *value, const std::vector<std::string> &list) {
	return value && std::find(list.begin(), list.end(), value) != list.end();
}

bool parse_int(const char *value, long long &out) {
	if (!value || *value == '\0')
		return false;

	char *end;
	errno = 0;
	long long result = strtoll(value, &end, 10);

	if (errno == ERANGE || end == value || *end != '\0')
		return false;

	out = result;
	return true;
}

// std::stoi-compatible: parses a leading integer and ignores the rest.
bool parse_int_prefix(const char *value, long long &out) {
	if (!value || *value == '\0')
		return false;

	char *end;
	errno = 0;
	long long result = strtoll(value, &end, 10);

	if (errno == ERANGE || end == value)
		return false;

	out = result;
	return true;
}

bool parse_double(const char *value, double &out) {
	if (!value || *value == '\0')
		return false;

	char *end;
	errno = 0;
	double result = strtod(value, &end);

	if (errno == ERANGE || end == value || *end != '\0')
		return false;

	out = result;
	return true;
}

// Split on every character of sep; consecutive delimiters compress like
// boost::token_compress_on.
void split_compress(const std::string &value, const std::string &sep, std::vector<std::string> &out) {
	std::string current;
	bool last_was_sep = false;

	for (const char c : value) {
		if (sep.find(c) != std::string::npos) {
			if (!last_was_sep)
				out.push_back(current);
			current.clear();
			last_was_sep = true;
		} else {
			current += c;
			last_was_sep = false;
		}
	}
	if (!last_was_sep)
		out.push_back(current);
}

// Count every delimiter character individually (no compression).
long long count_delims(const char *value, const std::string &sep) {
	long long count = 0;
	for (const char *c = value; *c; ++c)
		if (sep.find(*c) != std::string::npos)
			++count;
	return count;
}

bool cond_true(const RuleEngine::Condition &cond, const TagLookup &tags, const Maps &maps, Ctx &ctx) {
	const char *value = tags.get_value_by_key(cond.key.c_str());
	long long int_dummy;
	double double_dummy;

	switch (cond.op) {
		case Op::All:
			for (const auto &child : cond.children)
				if (!cond_true(*child, tags, maps, ctx))
					return false;
			return true;
		case Op::Any:
			for (const auto &child : cond.children)
				if (cond_true(*child, tags, maps, ctx))
					return true;
			return false;
		case Op::Not:
			return !cond_true(*cond.children.at(0), tags, maps, ctx);
		case Op::HasKey:
			return tags.has_key(cond.key.c_str());
		case Op::MissingKey:
			return !tags.has_key(cond.key.c_str());
		case Op::Eq:
			return value && cond.value == value;
		case Op::In:
			return string_in_list(value, cond.list ? *cond.list : cond.values);
		case Op::NotIn:
			return !string_in_list(value, cond.list ? *cond.list : cond.values);
		case Op::IsTrue:
			return string_in_list(value, TAG_TRUE);
		case Op::IsFalse:
			return string_in_list(value, TAG_FALSE);
		case Op::IsInt:
			return value && parse_int(value, int_dummy);
		case Op::NotInt:
			return !(value && parse_int(value, int_dummy));
		case Op::ParsesInt:
			return value && parse_int_prefix(value, int_dummy);
		case Op::NotParsesInt:
			return !(value && parse_int_prefix(value, int_dummy));
		case Op::IsDouble:
			return value && parse_double(value, double_dummy);
		case Op::NotDouble:
			return !(value && parse_double(value, double_dummy));
		case Op::Lt:
		case Op::Gt:
		case Op::LtEq:
		case Op::GtEq:
			if (!value)
				return false;
			if (cond.num_is_int) {
				long long num;
				if (!parse_int(value, num))
					return false;
				switch (cond.op) {
					case Op::Lt:   return num < cond.num_i;
					case Op::Gt:   return num > cond.num_i;
					case Op::LtEq: return num <= cond.num_i;
					default:       return num >= cond.num_i;
				}
			} else {
				double num;
				if (!parse_double(value, num))
					return false;
				switch (cond.op) {
					case Op::Lt:   return num < cond.num_d;
					case Op::Gt:   return num > cond.num_d;
					case Op::LtEq: return num <= cond.num_d;
					default:       return num >= cond.num_d;
				}
			}
		case Op::Fact:
			return tags.has_fact(cond.key.c_str());
		case Op::MapLookup: {
			if (!value || !cond.map)
				return false;
			auto found = cond.map->find(value);
			if (found == cond.map->end())
				return false;
			ctx["_mapped"] = found->second;
			return true;
		}
		case Op::MapNeq: {
			if (!value || !cond.map)
				return false;
			auto found = cond.map->find(value);
			if (found == cond.map->end())
				return false;
			const char *against = tags.get_value_by_key(cond.against.c_str());
			if (!against)
				return false;
			ctx["_mapped"] = found->second;
			return found->second != against;
		}
		case Op::ElementsNotMatch: {
			const char *other = tags.get_value_by_key(cond.other.c_str());
			if (!other)
				return false;
			long long mine;
			if (!parse_int(value, mine))
				mine = INT_MAX; // key_value_as_int() semantics
			long long count = count_delims(other, cond.sep) + 1;
			if (mine != count) {
				ctx["_other_key"] = cond.other;
				ctx["_other_value"] = other;
				return true;
			}
			return false;
		}
		case Op::AnyElementNotIn: {
			if (!value)
				return false;
			std::vector<std::string> parts;
			split_compress(value, cond.sep, parts);
			for (const auto &part : parts) {
				if (std::find(cond.values.begin(), cond.values.end(), part) == cond.values.end()) {
					ctx["_element"] = part;
					ctx["_source"] = value;
					return true;
				}
			}
			return false;
		}
		case Op::HasIndex:
		case Op::ElementEq: {
			if (!value)
				return false;
			std::vector<std::string> parts;
			split_compress(value, cond.sep, parts);
			if (static_cast<size_t>(cond.num_i) >= parts.size())
				return false;
			return cond.op == Op::HasIndex || parts[cond.num_i] == cond.value;
		}
		case Op::HasTurnInversion: {
			if (!value || !cond.map)
				return false;
			std::vector<std::string> parts;
			split_compress(value, cond.sep, parts);
			long long last = 99999;
			bool have_last = false;
			for (const auto &part : parts) {
				auto found = cond.map->find(part);
				if (found == cond.map->end())
					break;
				long long priority;
				if (!parse_int(found->second.c_str(), priority))
					break;
				if (have_last && priority > last) {
					ctx["_prev"] = parts[size_t(&part - parts.data()) - 1];
					ctx["_next"] = part;
					return true;
				}
				last = priority;
				have_last = true;
			}
			return false;
		}
		case Op::SumNeq: {
			if (!value)
				return false;
			long long mine;
			if (!parse_int(value, mine))
				return false;
			long long sum = 0;
			for (const auto &part : cond.parts) {
				const char *pv = tags.get_value_by_key(part.c_str());
				long long num;
				if (!pv || !parse_int(pv, num))
					return false;
				sum += num;
			}
			return mine != sum;
		}
	}

	return false;
}

std::unique_ptr<RuleEngine::Condition> clone_condition(const RuleEngine::Condition &src, const std::string &key) {
	auto cond = std::make_unique<RuleEngine::Condition>();

	auto expand = [&](const std::string &s) {
		std::string result;
		size_t pos = 0;
		while (true) {
			size_t at = s.find(KEY_PH, pos);
			if (at == std::string::npos) {
				result.append(s, pos, std::string::npos);
				break;
			}
			result.append(s, pos, at - pos);
			result += key;
			pos = at + KEY_PH.size();
		}
		return result;
	};

	cond->op = src.op;
	cond->key = expand(src.key);
	cond->value = src.value;
	cond->against = expand(src.against);
	cond->other = expand(src.other);
	cond->mapname = src.mapname;
	cond->sep = src.sep;
	cond->list = src.list;
	cond->values = src.values;
	cond->parts = src.parts;
	cond->map = src.map;
	cond->num_is_int = src.num_is_int;
	cond->num_i = src.num_i;
	cond->num_d = src.num_d;
	for (const auto &child : src.children)
		cond->children.push_back(clone_condition(*child, key));

	return cond;
}

// Replace ${key}/${value} (rules with "keys:"), context tokens set by
// evaluation-time operators (_mapped, _prev, _next, _element, _source,
// _other_key, _other_value) and plain tag tokens ${tagname}.
// Missing tokens are left untouched.
std::string substitute(const std::string &text, const std::string &expand_key,
		const TagLookup &tags, const Ctx &ctx) {
	std::string result;

	size_t pos = 0;
	while (true) {
		size_t start = text.find("${", pos);
		if (start == std::string::npos) {
			result.append(text, pos, std::string::npos);
			break;
		}
		size_t end = text.find('}', start + 2);
		if (end == std::string::npos) {
			result.append(text, pos, std::string::npos);
			break;
		}

		result.append(text, pos, start - pos);
		std::string token = text.substr(start + 2, end - start - 2);

		auto insert_value = [&](const char *v) {
			if (v)
				result += v;
			else
				result.append(text, start, end - start + 1);
		};
		auto insert_string = [&](const std::string &v) {
			result += v;
		};

		if (token == "key" && !expand_key.empty()) {
			insert_string(expand_key);
		} else if (token == "value" && !expand_key.empty()) {
			insert_value(tags.get_value_by_key(expand_key.c_str()));
		} else {
			auto found = ctx.find(token);
			if (found != ctx.end()) {
				insert_string(found->second);
			} else {
				insert_value(tags.get_value_by_key(token.c_str()));
			}
		}
		pos = end + 1;
	}

	return result;
}

std::unique_ptr<RuleEngine::Condition> parse_leaf(const std::string &opname, const YAML::Node &node,
		const Lists &lists, const Maps &maps) {
	auto cond = std::make_unique<RuleEngine::Condition>();

	// No explicit key: matching is against the key a "keys:" expansion provides.
	auto set_key = [&]() {
		cond->key = node["key"] ? node["key"].as<std::string>() : KEY_PH;
	};

	if (opname == "has_key" || opname == "missing_key" || opname == "is_int" || opname == "not_int"
			|| opname == "parses_int" || opname == "not_parses_int"
			|| opname == "is_double" || opname == "not_double"
			|| opname == "is_true" || opname == "is_false" || opname == "fact") {
		cond->key = node.as<std::string>();
		if (opname == "has_key") cond->op = Op::HasKey;
		else if (opname == "missing_key") cond->op = Op::MissingKey;
		else if (opname == "is_int") cond->op = Op::IsInt;
		else if (opname == "not_int") cond->op = Op::NotInt;
		else if (opname == "parses_int") cond->op = Op::ParsesInt;
		else if (opname == "not_parses_int") cond->op = Op::NotParsesInt;
		else if (opname == "is_double") cond->op = Op::IsDouble;
		else if (opname == "not_double") cond->op = Op::NotDouble;
		else if (opname == "is_true") cond->op = Op::IsTrue;
		else if (opname == "fact") cond->op = Op::Fact;
		else cond->op = Op::IsFalse;
		return cond;
	}

	if (opname == "eq") {
		cond->op = Op::Eq;
		set_key();
		if (!node["value"])
			throw std::runtime_error("eq needs a value");
		cond->value = node["value"].as<std::string>();
		return cond;
	}

	if (opname == "in" || opname == "not_in") {
		cond->op = (opname == "in") ? Op::In : Op::NotIn;
		set_key();
		if (node["list"]) {
			auto list = lists.find(node["list"].as<std::string>());
			if (list == lists.end())
				throw std::runtime_error("unknown list '" + node["list"].as<std::string>() + "'");
			cond->list = &list->second;
		} else if (node.IsScalar()) {
			auto list = lists.find(node.as<std::string>());
			if (list == lists.end())
				throw std::runtime_error("unknown list '" + node.as<std::string>() + "'");
			cond->list = &list->second;
		} else if (node["values"]) {
			for (const auto &val : node["values"])
				cond->values.push_back(val.as<std::string>());
		} else {
			throw std::runtime_error("in/not_in needs a list name or values");
		}
		return cond;
	}

	if (opname == "lt" || opname == "gt" || opname == "lte" || opname == "gte") {
		cond->op = (opname == "lt") ? Op::Lt : (opname == "gt") ? Op::Gt : (opname == "lte") ? Op::LtEq : Op::GtEq;
		set_key();
		if (!node["value"])
			throw std::runtime_error("numeric comparison needs a value");
		try {
			cond->num_i = node["value"].as<long long>();
			cond->num_is_int = true;
		} catch (const YAML::BadConversion &) {
			cond->num_d = node["value"].as<double>();
		}
		return cond;
	}

	if (opname == "map_lookup" || opname == "map_neq") {
		cond->op = (opname == "map_lookup") ? Op::MapLookup : Op::MapNeq;
		set_key();
		if (!node["map"])
			throw std::runtime_error(opname + " needs a map name");
		cond->mapname = node["map"].as<std::string>();
		auto map = maps.find(cond->mapname);
		if (map == maps.end())
			throw std::runtime_error("unknown map '" + cond->mapname + "'");
		cond->map = &map->second;
		if (cond->op == Op::MapNeq) {
			if (!node["against"])
				throw std::runtime_error("map_neq needs an against key");
			cond->against = node["against"].as<std::string>();
		}
		return cond;
	}

	if (opname == "elements_not_match") {
		cond->op = Op::ElementsNotMatch;
		set_key();
		if (!node["other"])
			throw std::runtime_error("elements_not_match needs an other key");
		cond->other = node["other"].as<std::string>();
		if (!node["sep"])
			throw std::runtime_error("elements_not_match needs a sep");
		cond->sep = node["sep"].as<std::string>();
		return cond;
	}

	if (opname == "any_element_not_in") {
		cond->op = Op::AnyElementNotIn;
		set_key();
		if (!node["sep"])
			throw std::runtime_error("any_element_not_in needs a sep");
		cond->sep = node["sep"].as<std::string>();
		if (node["list"]) {
			auto list = lists.find(node["list"].as<std::string>());
			if (list == lists.end())
				throw std::runtime_error("unknown list '" + node["list"].as<std::string>() + "'");
			cond->values = list->second;
		} else if (node["values"]) {
			for (const auto &val : node["values"])
				cond->values.push_back(val.as<std::string>());
		} else {
			throw std::runtime_error("any_element_not_in needs a list name or values");
		}
		return cond;
	}

	if (opname == "has_index" || opname == "element_eq") {
		cond->op = (opname == "has_index") ? Op::HasIndex : Op::ElementEq;
		set_key();
		if (!node["sep"])
			throw std::runtime_error(opname + " needs a sep");
		cond->sep = node["sep"].as<std::string>();
		if (!node["index"])
			throw std::runtime_error(opname + " needs an index");
		cond->num_i = node["index"].as<int>();
		if (cond->op == Op::ElementEq) {
			if (!node["value"])
				throw std::runtime_error("element_eq needs a value");
			cond->value = node["value"].as<std::string>();
		}
		return cond;
	}

	if (opname == "has_turn_inversion") {
		cond->op = Op::HasTurnInversion;
		set_key();
		if (!node["sep"])
			throw std::runtime_error("has_turn_inversion needs a sep");
		cond->sep = node["sep"].as<std::string>();
		if (!node["map"])
			throw std::runtime_error("has_turn_inversion needs a map");
		cond->mapname = node["map"].as<std::string>();
		auto map = maps.find(cond->mapname);
		if (map == maps.end())
			throw std::runtime_error("unknown map '" + cond->mapname + "'");
		cond->map = &map->second;
		return cond;
	}

	if (opname == "sum_neq") {
		cond->op = Op::SumNeq;
		set_key();
		if (!node["parts"])
			throw std::runtime_error("sum_neq needs parts");
		for (const auto &part : node["parts"])
			cond->parts.push_back(part.as<std::string>());
		return cond;
	}

	throw std::runtime_error("unknown operator '" + opname + "'");
}

std::unique_ptr<RuleEngine::Condition> parse_when(const YAML::Node &node, const Lists &lists, const Maps &maps) {
	if (!node || node.IsNull())
		throw std::runtime_error("missing 'when'");

	if (!node.IsSequence() && !node.IsMap())
		throw std::runtime_error("'when' must be a mapping or sequence");

	std::unique_ptr<RuleEngine::Condition> cond;

	if (node.IsSequence()) {
		cond = std::make_unique<RuleEngine::Condition>();
		cond->op = Op::All;
		for (const auto &entry : node)
			cond->children.push_back(parse_when(entry, lists, maps));
	} else {
		cond = std::make_unique<RuleEngine::Condition>();
		cond->op = Op::All;

		for (YAML::const_iterator entry = node.begin(); entry != node.end(); ++entry) {
			std::string name = entry->first.as<std::string>();

			if (name == "all" || name == "any") {
				auto combinator = std::make_unique<RuleEngine::Condition>();
				combinator->op = (name == "all") ? Op::All : Op::Any;
				YAML::Node children = entry->second;
				if (children.IsSequence()) {
					for (const auto &child : children)
						combinator->children.push_back(parse_when(child, lists, maps));
				} else {
					combinator->children.push_back(parse_when(children, lists, maps));
				}
				if (combinator->children.empty())
					throw std::runtime_error("empty " + name);
				cond->children.push_back(std::move(combinator));
			} else if (name == "not") {
				auto negation = std::make_unique<RuleEngine::Condition>();
				negation->op = Op::Not;
				negation->children.push_back(parse_when(entry->second, lists, maps));
				cond->children.push_back(std::move(negation));
			} else {
				cond->children.push_back(parse_leaf(name, entry->second, lists, maps));
			}
		}
	}

	if (cond->children.empty())
		throw std::runtime_error("empty 'when'");

	return cond;
}

const YAML::Node get_field(const YAML::Node &parent, const char *key) {
	const YAML::Node &node = parent[key];
	if (!node)
		throw std::runtime_error(std::string("missing '") + key + "'");
	return node;
}

void add_report(const YAML::Node &item, std::vector<RuleEngine::ReportItem> &reports) {
	RuleEngine::ReportItem report;

	if (!item || !item.IsMap())
		throw std::runtime_error("'report' must be a mapping");

	report.layer = get_field(item, "layer").as<std::string>();
	report.style = item["style"] ? item["style"].as<std::string>() : "default";
	report.problem = get_field(item, "problem").as<std::string>();
	reports.push_back(std::move(report));
}

} // namespace

void RuleEngine::set_known_layers(const std::vector<std::string> &layers) {
	m_layers = layers;

	std::function<void(const Rule &)> check = [&](const Rule &rule) {
		for (const auto &report : rule.reports)
			if (std::find(m_layers.begin(), m_layers.end(), report.layer) == m_layers.end())
				throw std::runtime_error("rule '" + rule.id + "' reports to unknown layer '" + report.layer + "'");
		for (const auto &child : rule.rules)
			check(*child);
	};

	for (const auto &rule : m_rules)
		check(*rule);
}

void RuleEngine::evaluate(Sink &sink, const TagLookup &tags) {
	std::map<std::string, std::string> gate_ctx;
	++m_ways;

	if (m_gate) {
		if (cond_true(*m_gate, tags, m_maps, gate_ctx))
			++m_gate_hits;
		else {
			++m_gate_misses;
			return;
		}
	}

	for (const auto &rule : m_rules)
		eval_rule(*rule, sink, tags, std::map<std::string, std::string>{});
}

// Recursive rule evaluation. Nested rules run only when the parent condition
// matched and inherit the context tokens set during its condition evaluation
// (whoever sets a token last inside a condition wins); every child starts from
// the parent's state, so siblings never see each other's tokens.
void RuleEngine::eval_rule(Rule &rule, Sink &sink, const TagLookup &tags,
		std::map<std::string, std::string> ctx) {
	if (cond_true(*rule.condition, tags, m_maps, ctx)) {
		++rule.hits;
	} else {
		++rule.misses;
		return;
	}

	for (const auto &report : rule.reports)
		sink.report(report.layer, report.style,
				substitute(report.problem, rule.expand_key, tags, ctx));

	for (const auto &child : rule.rules)
		eval_rule(*child, sink, tags, ctx);
}

namespace {

// One " rate  %  hits  misses  <indented id>" line.
void stats_line(std::ostream &out, unsigned long hits, unsigned long misses,
		int depth, const std::string &id) {
	unsigned long total = hits + misses;
	double rate = total ? 100.0 * double(hits) / double(total) : 0.0;

	out << std::fixed << std::setprecision(2) << std::setw(8) << rate << " %"
		<< std::setw(10) << hits
		<< std::setw(10) << misses
		<< std::string(2 * depth + 2, ' ')
		<< id << '\n';
}

} // namespace

void RuleEngine::dump_stats_line(const Rule &rule, int depth, std::ostream &out) const {
	stats_line(out, rule.hits, rule.misses, depth, rule.id);
}

void RuleEngine::dump_stats(std::ostream &out) const {
	std::ios saved(nullptr);
	saved.copyfmt(out);

	out << "\n== rule statistics (" << m_ways << " ways evaluated; hit = \"when\" matched)\n";
	stats_line(out, m_gate_hits, m_gate_misses, 0, "(gate)");

	std::function<void(const Rule &, int)> walk = [&](const Rule &rule, int depth) {
		dump_stats_line(rule, depth, out);
		for (const auto &child : rule.rules)
			walk(*child, depth + 1);
	};
	for (const auto &rule : m_rules)
		walk(*rule, 0);

	out.copyfmt(saved);
}

void RuleEngine::load(const std::string &path) {
	YAML::Node doc;
	try {
		doc = YAML::LoadFile(path);
	} catch (const YAML::Exception &error) {
		throw std::runtime_error(path + ": " + error.what());
	}

	if (const YAML::Node &lists = doc["lists"]) {
		for (YAML::const_iterator entry = lists.begin(); entry != lists.end(); ++entry) {
			std::vector<std::string> values;
			for (const auto &value : entry->second)
				values.push_back(value.as<std::string>());
			m_lists[entry->first.as<std::string>()] = std::move(values);
		}
	}

	if (const YAML::Node &maps = doc["maps"]) {
		for (YAML::const_iterator entry = maps.begin(); entry != maps.end(); ++entry) {
			std::map<std::string, std::string> entries;
			for (YAML::const_iterator kv = entry->second.begin(); kv != entry->second.end(); ++kv)
				entries[kv->first.as<std::string>()] = kv->second.as<std::string>();
			m_maps[entry->first.as<std::string>()] = std::move(entries);
		}
	}

	if (const YAML::Node &gate = doc["gate"])
		m_gate = parse_when(gate, m_lists, m_maps);

	const YAML::Node &rules = doc["rules"];
	if (!rules || !rules.IsSequence())
		throw std::runtime_error(path + ": missing 'rules' section");

	// Recursive rule parser: a rule carries "when", an optional "report"
	// (grouping rules may leave the reporting to children) and optional
	// nested "rules".
	std::function<std::unique_ptr<Rule>(const YAML::Node &rule, const std::string &id, bool allow_keys)> parse_rule;
	parse_rule = [&](const YAML::Node &rule, const std::string &id,
			bool allow_keys) -> std::unique_ptr<Rule> {
		auto out = std::make_unique<Rule>();
		out->id = id;

		// "keys:" expansion is done by the load loop below; nested rules with
		// {key} templates are expanded via the parent rule's key.
		if (rule["keys"] && !allow_keys)
			throw std::runtime_error("nested rule cannot use 'keys' - put it on the parent rule");

		out->condition = parse_when(get_field(rule, "when"), m_lists, m_maps);

		if (const YAML::Node &report = rule["report"]) {
			if (report.IsSequence()) {
				for (const auto &item : report)
					add_report(item, out->reports);
			} else {
				add_report(report, out->reports);
			}
		}

		if (const YAML::Node &nested = rule["rules"]) {
			unsigned int num = 0;
			for (const auto &child : nested) {
				++num;
				std::string child_id = child["id"] ? child["id"].as<std::string>() : id + "#" + std::to_string(num);
				try {
					out->rules.push_back(parse_rule(child, child_id, false));
				} catch (const YAML::Exception &error) {
					throw std::runtime_error("rule '" + child_id + "': " + error.what());
				} catch (const std::exception &error) {
					throw std::runtime_error("rule '" + child_id + "': " + error.what());
				}
			}
		}

		if (out->reports.empty() && out->rules.empty())
			throw std::runtime_error("needs a report or nested rules");

		return out;
	};

	// Deep copy for "keys:" expansion; conditions and problem texts of nested
	// rules may use "{key}" templates as well.
	std::function<std::unique_ptr<Rule>(const Rule &src, const std::string &key, const std::string &id)> clone_rule;
	clone_rule = [&](const Rule &src, const std::string &key, const std::string &id) {
		auto out = std::make_unique<Rule>();
		out->id = id;
		out->expand_key = key;
		out->condition = clone_condition(*src.condition, key);
		out->reports = src.reports;
		for (const auto &child : src.rules)
			out->rules.push_back(clone_rule(*child, key, child->id));
		return out;
	};

	unsigned int num = 0;
	for (const auto &rule : rules) {
		++num;

		std::string id;

		try {
			// May throw before id is known - use the index.
			id = rule["id"] ? rule["id"].as<std::string>() : "rule#" + std::to_string(num);
			auto parsed = parse_rule(rule, id, true);

			if (const YAML::Node &keys = rule["keys"]) {
				std::vector<std::string> keynames;
				if (keys.IsMap()) {
					auto list = m_lists.find(get_field(keys, "list").as<std::string>());
					if (list == m_lists.end())
						throw std::runtime_error("unknown list '" + keys["list"].as<std::string>() + "'");
					keynames = list->second;
				} else if (keys.IsScalar()) {
					auto list = m_lists.find(keys.as<std::string>());
					if (list == m_lists.end())
						throw std::runtime_error("unknown list '" + keys.as<std::string>() + "'");
					keynames = list->second;
				} else {
					for (const auto &key : keys)
						keynames.push_back(key.as<std::string>());
				}

				for (const auto &keyname : keynames)
					m_rules.push_back(clone_rule(*parsed, keyname, id + ":" + keyname));
			} else {
				m_rules.push_back(std::move(parsed));
			}
		} catch (const YAML::Exception &error) {
			throw std::runtime_error(path + ": rule '" + id + "': " + error.what());
		} catch (const std::exception &error) {
			throw std::runtime_error(path + ": rule '" + id + "': " + error.what());
		}
	}
}
