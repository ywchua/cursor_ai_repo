## DBW Rules Engine (Config-driven)

Build:

```bash
mkdir -p build && cd build
cmake ..
cmake --build . -j
```

Run example with the provided rules:

```bash
./dbw_rules_example ../config/example_rules.json
```

Notes:
- If `nlohmann_json` is not installed, the example falls back to a programmatic rule.
- JSON Schema is in `config/rules.schema.json` for validating rule files.

