// SPDX-License-Identifier: MIT
#include "dbw/RuleEngine.hpp"

#include <iostream>
#include <thread>

using namespace dbw;

class FakeSignals : public SignalProvider {
public:
	bool getNumber(const std::string &name, double &out) const override {
		auto it = numbers.find(name);
		if (it == numbers.end()) return false;
		out = it->second; return true;
	}
	bool getString(const std::string &name, std::string &out) const override {
		auto it = strings.find(name);
		if (it == strings.end()) return false;
		out = it->second; return true;
	}
	bool getBool(const std::string &name, bool &out) const override {
		auto it = bools.find(name);
		if (it == bools.end()) return false;
		out = it->second; return true;
	}
	std::unordered_map<std::string, double> numbers;
	std::unordered_map<std::string, std::string> strings;
	std::unordered_map<std::string, bool> bools;
};

static void printCommands(const CommandBuffer &buf) {
	for (const auto &kv : buf.commands()) {
		std::cout << "set { " << kv.first << ": ";
		if (std::holds_alternative<double>(kv.second))
			std::cout << std::get<double>(kv.second);
		else if (std::holds_alternative<bool>(kv.second))
			std::cout << (std::get<bool>(kv.second) ? "true" : "false");
		else
			std::cout << '"' << std::get<std::string>(kv.second) << '"';
		std::cout << " }\n";
	}
}

int main(int argc, char **argv) {
	RuleEngine engine;
	std::string error;
	bool loaded = false;
	#if defined(DBW_USE_NLOHMANN_JSON)
	if (argc > 1) {
		loaded = engine.loadRulesFromFile(argv[1], error);
		if (!loaded)
			std::cerr << "Failed to load rules: " << error << "\n";
		else
			std::cout << "Rules loaded successfully from " << argv[1] << "\n";
	}
	#endif
	if (!loaded) {
		// Fallback programmatic rule matching the user's example (enter_auto_mode)
		Rule r;
		r.name = "enter_auto_mode";
		r.trigger = "dbw_toggle_on";
		r.conditions = {
			Condition{ "gear", CompareOp::Eq, std::string("P") },
			Condition{ "VCU_Life_Signal", CompareOp::Gt, 0.0 }
		};
		r.sequence = {
			StepSet{ "AS_HandShank_Ctrl_St", 0.0 },
			StepSet{ "AS_Strg0_Enable", 3.0 },
			StepSet{ "AS_Strg1_Enable", 3.0 },
			StepSet{ "AS_Strg_WorkMode_Req", 1.0 },
			StepSet{ "AS_AutoD_Shift_Req", 2.0 },
			StepSet{ "AS_Longit_Ctrlmode", 1.0 },
			StepSet{ "AS_AutoD_BrkMode_Req", 3.0 },
			StepSet{ "AS_AutoD_Accel_Pos_Req", 0.0 },
			StepSet{ "AS_AutoD_BrkPelPos_Req", 0.3 },
			StepSet{ "AS_AutoD_Spd_Limit", 5.0 },
			StepWait{ Milli(200) },
			StepSet{ "AS_AutoD_Req", 1.0 }
		};
		engine.setRules({std::move(r)});
	}

	FakeSignals sig;
	sig.strings["gear"] = "P";
	sig.numbers["VCU_Life_Signal"] = 1.0;

	CommandBuffer out;
	engine.onEvent("dbw_toggle_on");

	for (int i = 0; i < 20; ++i) {
		out.clear();
		engine.tick(sig, out);
		if (!out.commands().empty()) {
			printCommands(out);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	return 0;
}

