#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>
#include <deque>

namespace dbw {

using SteadyClock = std::chrono::steady_clock;
using Milli = std::chrono::milliseconds;

using Value = std::variant<bool, double, std::string>;

inline std::string valueTypeName(const Value &v) {
	if (std::holds_alternative<bool>(v)) return "bool";
	if (std::holds_alternative<double>(v)) return "number";
	return "string";
}

enum class SteerMode : uint8_t {
	Disabled          = 0b000001,
	Enabled           = 0b110001,
	RecoverEnabled    = 0b110101
};

enum class DriveMode : uint8_t {
	Disabled = 0b0000,
	Enabled  = 0b0101
};

enum class Gear { R, N, D };

inline uint8_t encodeGear(Gear g) {
	switch (g) {
		case Gear::R: return 0b010001;
		case Gear::N: return 0b010010;
		case Gear::D: return 0b010011;
	}
	return 0b010010;
}

class SignalProvider {
public:
	virtual ~SignalProvider() = default;
	virtual bool getNumber(const std::string &name, double &out) const = 0;
	virtual bool getString(const std::string &name, std::string &out) const = 0;
	virtual bool getBool(const std::string &name, bool &out) const = 0;
};

class CommandBuffer {
public:
	void set(const std::string &key, const Value &value) { commands_[key] = value; }
	const std::unordered_map<std::string, Value>& commands() const { return commands_; }
	void clear() { commands_.clear(); }
private:
	std::unordered_map<std::string, Value> commands_;
};

enum class CompareOp { Eq, Ne, Gt, Lt, Ge, Le };

struct Condition {
	std::string signal;
	CompareOp op { CompareOp::Eq };
	Value value { false };
};

struct StepSet { std::string key; Value value; };
struct StepWait { Milli duration {0}; };
struct StepSetState { std::string key; Value value; };
struct StepEmitEvent { std::string name; };
using Step = std::variant<StepSet, StepWait, StepSetState, StepEmitEvent>;

struct Rule {
	std::string name;
	std::string trigger;                 // event name, e.g., "dbw_toggle_on"
	std::vector<Condition> conditions;   // all must pass to start/continue
	std::vector<Step> sequence;          // executed across ticks
};

struct ActiveSequence {
	const Rule* rule { nullptr };
	size_t stepIndex { 0 };
	SteadyClock::time_point stepStarted;
	SteadyClock::time_point startedAt;
};

class RuleEngine {
public:
	void setRules(std::vector<Rule> rules);
	void onEvent(std::string_view eventName);
	void tick(const SignalProvider &signals, CommandBuffer &out);
	void tickAt(const SignalProvider &signals, CommandBuffer &out, SteadyClock::time_point now);

	std::optional<std::string> activeRuleName() const;
	size_t activeStepIndex() const;

#if defined(DBW_USE_NLOHMANN_JSON)
	bool loadRulesFromFile(const std::string &path, std::string &errorMessage);
	bool loadRulesFromJsonString(const std::string &jsonText, std::string &errorMessage);
#endif

private:
	bool conditionsSatisfied(const Rule &rule, const SignalProvider &signals) const;
	bool evaluate(const Condition &c, const SignalProvider &signals) const;
	static bool compare(const Value &lhs, CompareOp op, const Value &rhs);
	static std::optional<double> asNumber(const Value &v);
	static std::optional<std::string> asString(const Value &v);
	static std::optional<bool> asBool(const Value &v);

	void advanceStep(SteadyClock::time_point now);

	std::vector<Rule> rules_;
	std::optional<ActiveSequence> active_;
	std::deque<std::string> eventQueue_;
	std::unordered_map<std::string, Value> state_;
};

} // namespace dbw