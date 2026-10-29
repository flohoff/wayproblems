#ifndef WAYPROBLEMS_YAMLRULES_HPP
#define WAYPROBLEMS_YAMLRULES_HPP

#include <cstddef>
#include <iosfwd>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Minimal read-only tag access so the rule engine needs no osmium headers.
// Facts carry non-tag evaluations (e.g. way geometry).
class TagLookup {
	public:
		virtual ~TagLookup() = default;
		virtual bool has_key(const char *key) const = 0;
		virtual const char *get_value_by_key(const char *key) const = 0;
		virtual bool has_fact(const char *fact) const { return false; }
};

class RuleEngine {
	public:
		struct Sink {
				virtual void report(const std::string &layer, const std::string &style, const std::string &problem) = 0;
				virtual ~Sink() = default;
		};

		RuleEngine();
		~RuleEngine(); // Condition is an incomplete type in the header
		RuleEngine(const RuleEngine &) = delete;
		RuleEngine &operator=(const RuleEngine &) = delete;

		// Throws std::runtime_error on invalid config.
		void load(const std::string &path);

		// Validates report layers; call after writer setup, throws on unknown layer.
		void set_known_layers(const std::vector<std::string> &layers);

		// Per-rule hit/miss statistics; "hit" = the rule condition matched.
		void dump_stats(std::ostream &out) const;

		void evaluate(Sink &sink, const TagLookup &tags);
		size_t size() const { return m_rules.size(); }

		struct ReportItem { std::string layer, style, problem; };

		struct Condition;

	private:
		// expand_key: concrete key for rules created via "keys:" expansion,
		// enables ${key}/${value} substitution in problem text.
		struct Rule {
				std::string id;
				std::string expand_key;
				std::unique_ptr<Condition> condition;
				std::vector<ReportItem> reports;
				// Nested rules are evaluated only when this rule's condition
				// matches; they inherit this rule's context tokens.
				std::vector<std::unique_ptr<Rule>> rules;
				unsigned long hits = 0;
				unsigned long misses = 0;

				~Rule(); // Condition is incomplete here, defined in yamlrules.cpp
		};

		std::map<std::string, std::vector<std::string>> m_lists;
		std::map<std::string, std::map<std::string, std::string>> m_maps;
		std::vector<std::unique_ptr<Rule>> m_rules;
		std::vector<std::string> m_layers;
		std::unique_ptr<Condition> m_gate;
		unsigned long m_ways = 0;
		unsigned long m_gate_hits = 0;
		unsigned long m_gate_misses = 0;

		void eval_rule(Rule &rule, Sink &sink, const TagLookup &tags,
				std::map<std::string, std::string> ctx);
		void dump_stats_line(const Rule &rule, int depth, std::ostream &out) const;
};

#endif
