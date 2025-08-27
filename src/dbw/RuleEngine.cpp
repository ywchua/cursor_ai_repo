// SPDX-License-Identifier: MIT
#include "dbw/RuleEngine.hpp"

#include <cmath>
#include <fstream>
#include <sstream>

#if defined(DBW_USE_NLOHMANN_JSON)
#include <nlohmann/json.hpp>
#endif

namespace dbw {

void RuleEngine::setRules(std::vector<Rule> rules) {
	rules_ = std::move(rules);
	active_.reset();
	pendingEvent_.reset();
}

void RuleEngine::onEvent(std::string_view eventName) {
	pendingEvent_ = std::string(eventName);
}

void RuleEngine::tick(const SignalProvider &signals, CommandBuffer &out) {
	tickAt(signals, out, SteadyClock::now());
}

void RuleEngine::tickAt(const SignalProvider &signals, CommandBuffer &out, SteadyClock::time_point now) {
	// Start a new sequence if none active and an event is pending
	if (!active_.has_value() && pendingEvent_.has_value()) {
		const std::string event = *pendingEvent_;
		pendingEvent_.reset();
		for (const Rule &r : rules_) {
			if (r.trigger == event && conditionsSatisfied(r, signals)) {
				active_ = ActiveSequence{ &r, 0, now, now };
				break;
			}
		}
	}

	if (!active_.has_value()) return;

	// Cancel if conditions no longer satisfied
	if (!conditionsSatisfied(*active_->rule, signals)) {
		active_.reset();
		return;
	}

	// Execute current step
	if (active_->stepIndex >= active_->rule->sequence.size()) {
		active_.reset();
		return;
	}

	const Step &step = active_->rule->sequence[active_->stepIndex];
	if (std::holds_alternative<StepSet>(step)) {
		const StepSet &s = std::get<StepSet>(step);
		out.set(s.key, s.value);
		advanceStep(now);
		return;
	}

	if (std::holds_alternative<StepWait>(step)) {
		const StepWait &w = std::get<StepWait>(step);
		if (now - active_->stepStarted >= w.duration) {
			advanceStep(now);
		}
	}
}

std::optional<std::string> RuleEngine::activeRuleName() const {
	if (!active_.has_value()) return std::nullopt;
	return active_->rule->name;
}

size_t RuleEngine::activeStepIndex() const {
	if (!active_.has_value()) return 0;
	return active_->stepIndex;
}

bool RuleEngine::conditionsSatisfied(const Rule &rule, const SignalProvider &signals) const {
	for (const Condition &c : rule.conditions) {
		if (!evaluate(c, signals)) return false;
	}
	return true;
}

bool RuleEngine::evaluate(const Condition &c, const SignalProvider &signals) {
	// Try number, bool, then string comparisons
	double n{}; bool b{}; std::string s;
	if (signals.getNumber(c.signal, n) && std::holds_alternative<double>(c.value))
		return compare(n, c.op, std::get<double>(c.value));
	if (signals.getBool(c.signal, b) && std::holds_alternative<bool>(c.value))
		return compare(b, c.op, std::get<bool>(c.value));
	if (signals.getString(c.signal, s) && std::holds_alternative<std::string>(c.value))
		return compare(s, c.op, std::get<std::string>(c.value));
	// Type mismatch or signal unavailable -> fail the condition
	return false;
}

static int compareDoubles(double a, double b) {
	const double eps = 1e-6;
	double d = a - b;
	if (std::fabs(d) <= eps) return 0;
	return (d < 0) ? -1 : 1;
}

bool RuleEngine::compare(const Value &lhs, CompareOp op, const Value &rhs) {
	// bool
	if (std::holds_alternative<bool>(lhs) && std::holds_alternative<bool>(rhs)) {
		bool a = std::get<bool>(lhs), b = std::get<bool>(rhs);
		switch (op) {
			case CompareOp::Eq: return a == b;
			case CompareOp::Ne: return a != b;
			default: return false; // other ops not meaningful for bool
		}
	}
	// number
	if (std::holds_alternative<double>(lhs) && std::holds_alternative<double>(rhs)) {
		double a = std::get<double>(lhs), b = std::get<double>(rhs);
		int c = compareDoubles(a, b);
		switch (op) {
			case CompareOp::Eq: return c == 0;
			case CompareOp::Ne: return c != 0;
			case CompareOp::Gt: return c > 0;
			case CompareOp::Lt: return c < 0;
			case CompareOp::Ge: return c >= 0;
			case CompareOp::Le: return c <= 0;
		}
	}
	// string
	if (std::holds_alternative<std::string>(lhs) && std::holds_alternative<std::string>(rhs)) {
		const std::string &a = std::get<std::string>(lhs);
		const std::string &b = std::get<std::string>(rhs);
		switch (op) {
			case CompareOp::Eq: return a == b;
			case CompareOp::Ne: return a != b;
			default: return false; // others not meaningful for strings here
		}
	}
	return false;
}

std::optional<double> RuleEngine::asNumber(const Value &v) {
	if (std::holds_alternative<double>(v)) return std::get<double>(v);
	return std::nullopt;
}

std::optional<std::string> RuleEngine::asString(const Value &v) {
	if (std::holds_alternative<std::string>(v)) return std::get<std::string>(v);
	return std::nullopt;
}

std::optional<bool> RuleEngine::asBool(const Value &v) {
	if (std::holds_alternative<bool>(v)) return std::get<bool>(v);
	return std::nullopt;
}

void RuleEngine::advanceStep(SteadyClock::time_point now) {
	if (!active_.has_value()) return;
	active_->stepIndex++;
	active_->stepStarted = now;
	if (active_->stepIndex >= active_->rule->sequence.size()) {
		active_.reset();
	}
}

#if defined(DBW_USE_NLOHMANN_JSON)

static CompareOp parseOp(const std::string &key) {
	if (key == "equals") return CompareOp::Eq;
	if (key == "not_equals") return CompareOp::Ne;
	if (key == "greater_than") return CompareOp::Gt;
	if (key == "less_than") return CompareOp::Lt;
	if (key == "greater_or_equal") return CompareOp::Ge;
	if (key == "less_or_equal") return CompareOp::Le;
	return CompareOp::Eq;
}

static Value parseValue(const nlohmann::json &j) {
	if (j.is_boolean()) return j.get<bool>();
	if (j.is_number()) return j.get<double>();
	return j.get<std::string>();
}

static bool parseRule(const nlohmann::json &j, Rule &out, std::string &err) {
	if (!j.contains("name") || !j.contains("trigger") || !j.contains("sequence")) {
		err = "rule missing required fields";
		return false;
	}
	out.name = j.at("name").get<std::string>();
	out.trigger = j.at("trigger").at("on_event").get<std::string>();

	// conditions
	out.conditions.clear();
	if (j.contains("conditions")) {
		for (const auto &cj : j.at("conditions")) {
			Condition c;
			c.signal = cj.at("signal").get<std::string>();
			// find operator key
			for (auto it = cj.begin(); it != cj.end(); ++it) {
				const std::string key = it.key();
				if (key == "signal") continue;
				c.op = parseOp(key);
				c.value = parseValue(it.value());
				break;
			}
			out.conditions.push_back(std::move(c));
		}
	}

	// sequence
	out.sequence.clear();
	for (const auto &sj : j.at("sequence")) {
		if (sj.contains("set")) {
			const auto &setj = sj.at("set");
			if (setj.size() != 1) { err = "set step must have exactly one key"; return false; }
			auto it = setj.begin();
			StepSet s{ it.key(), parseValue(it.value()) };
			out.sequence.emplace_back(std::move(s));
		} else if (sj.contains("wait_ms")) {
			StepWait w{ Milli(sj.at("wait_ms").get<int>()) };
			out.sequence.emplace_back(std::move(w));
		} else {
			err = "unknown step type";
			return false;
		}
	}
	return true;
}

bool RuleEngine::loadRulesFromFile(const std::string &path, std::string &errorMessage) {
	std::ifstream ifs(path);
	if (!ifs) { errorMessage = "cannot open file: " + path; return false; }
	std::stringstream buffer; buffer << ifs.rdbuf();
	return loadRulesFromJsonString(buffer.str(), errorMessage);
}

bool RuleEngine::loadRulesFromJsonString(const std::string &jsonText, std::string &errorMessage) {
	nlohmann::json j;
	try { j = nlohmann::json::parse(jsonText); }
	catch (const std::exception &e) { errorMessage = e.what(); return false; }

	if (!j.contains("rules") || !j.at("rules").is_array()) {
		errorMessage = "missing 'rules' array";
		return false;
	}
	std::vector<Rule> parsed;
	for (const auto &rj : j.at("rules")) {
		Rule r;
		if (!parseRule(rj, r, errorMessage)) return false;
		parsed.push_back(std::move(r));
	}
	setRules(std::move(parsed));
	return true;
}

#endif // DBW_USE_NLOHMANN_JSON

} // namespace dbw

