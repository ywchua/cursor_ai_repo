// SPDX-License-Identifier: MIT
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

namespace dbw {

using SteadyClock = std::chrono::steady_clock;
using Milli = std::chrono::milliseconds;

// Generic value type for signals and set-commands
using Value = std::variant<bool, double, std::string>;

inline std::string valueTypeName(const Value &v) {
	if (std::holds_alternative<bool>(v)) return "bool";
	if (std::holds_alternative<double>(v)) return "number";
	return "string";
}

// Encoder helpers/examples for your existing bitfields (optional to use here)
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

// Signals interface to read current system state for condition evaluation
class SignalProvider {
public:
	virtual ~SignalProvider() = default;
	virtual bool getNumber(const std::string &name, double &out) const = 0;
	virtual bool getString(const std::string &name, std::string &out) const = 0;
	virtual bool getBool(const std::string &name, bool &out) const = 0;
};

// Command buffer to accumulate set-operations for the current control tick
class CommandBuffer {
public:
	// Adds or overwrites a key with a value to be applied this cycle
	void set(const std::string &key, const Value &value) { commands_[key] = value; }

	// Returns a snapshot of the commands for this tick
	const std::unordered_map<std::string, Value>& commands() const { return commands_; }

	// Clears the buffer (call between ticks)
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
using Step = std::variant<StepSet, StepWait>;

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

	// Records an external event; processed on next tick
	void onEvent(std::string_view eventName);

	// Progresses the active sequence and emits set-commands into out
	void tick(const SignalProvider &signals, CommandBuffer &out);
	void tickAt(const SignalProvider &signals, CommandBuffer &out, SteadyClock::time_point now);

	// Introspection
	std::optional<std::string> activeRuleName() const;
	size_t activeStepIndex() const;

#if defined(DBW_USE_NLOHMANN_JSON)
	// Load rules from JSON file/string
	// Returns false and sets error if parsing fails
	bool loadRulesFromFile(const std::string &path, std::string &errorMessage);
	bool loadRulesFromJsonString(const std::string &jsonText, std::string &errorMessage);
#endif

private:
	bool conditionsSatisfied(const Rule &rule, const SignalProvider &signals) const;
	static bool evaluate(const Condition &c, const SignalProvider &signals);
	static bool compare(const Value &lhs, CompareOp op, const Value &rhs);
	static std::optional<double> asNumber(const Value &v);
	static std::optional<std::string> asString(const Value &v);
	static std::optional<bool> asBool(const Value &v);

	void advanceStep(SteadyClock::time_point now);

	std::vector<Rule> rules_;
	std::optional<ActiveSequence> active_;
	std::optional<std::string> pendingEvent_;
};

} // namespace dbw

