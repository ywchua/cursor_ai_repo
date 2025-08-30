## CAN Bus Emulator (VCU Test Harness)

This is a lightweight Python CAN bus emulator to test a VCU program. It publishes fake vehicle feedback signals on a CAN interface and updates its internal state based on commands sent from the VCU over CAN.

### Features
- Periodic TX of feedback signals: speed, motor RPM, battery SoC
- RX of commands: enable/disable, target speed
- Simple vehicle dynamics model (speed ramps toward target)
- CLI configuration (channel, periods, duration, log level)
- Optional self-test mode to inject a command for quick sanity checks
- SocketCAN/vcan support (Linux)

### Requirements
- Linux with SocketCAN
- Python 3.9+

### Setup a virtual CAN interface
```bash
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
ip link show vcan0
```

### Install dependencies
```bash
python -m pip install -U pip
python -m pip install -r requirements.txt
```

### Run the emulator
```bash
python -m can_emulator run --channel vcan0 --tx-period-ms 100 --duration 10
```

Add `--self-test` to have the emulator inject a demo command after 1s:
```bash
python -m can_emulator run --channel vcan0 --self-test --duration 5
```

### Interacting with the emulator
The emulator publishes on these default IDs (standard 11-bit):
- 0x100: speed feedback (uint16, 0.1 km/h units)
- 0x101: motor RPM feedback (uint16)
- 0x102: battery SoC (uint8, %)

It listens for commands:
- 0x200: target speed command (uint16, 0.1 km/h units)
- 0x201: enable/disable (uint8: 1=enable, 0=disable)

Example using can-utils (if installed):
```bash
cansend vcan0 201#01          # enable
cansend vcan0 200#e803        # set target speed to 1000 (100.0 km/h little-endian: 0x03e8)
```

### Optional config file
You can pass a YAML config to override IDs, periods, or initial state:
```yaml
ids:
  speed_feedback: 0x100
  rpm_feedback: 0x101
  soc_feedback: 0x102
  target_speed_command: 0x200
  enable_command: 0x201
periods_ms:
  tx_feedback: 100
initial_state:
  speed_kph: 0.0
  target_speed_kph: 0.0
  rpm: 0
  soc_percent: 85
```

Run with:
```bash
python -m can_emulator run --config sample_config.yaml --duration 10
```

### Notes
- Units: speed is encoded as 0.1 km/h (uint16 LE), RPM as uint16, SoC as uint8.
- The simple model ramps speed toward the target at a fixed acceleration and drains SoC at a rate proportional to speed.
- vcan ignores bitrate; real hardware will require appropriate bitrate configuration.

