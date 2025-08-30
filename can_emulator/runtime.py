import logging
import struct
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, Optional

import can
import yaml


logger = logging.getLogger("can_emulator")


@dataclass
class IdConfig:
    speed_feedback: int = 0x100
    rpm_feedback: int = 0x101
    soc_feedback: int = 0x102
    target_speed_command: int = 0x200
    enable_command: int = 0x201


@dataclass
class PeriodsConfig:
    tx_feedback: int = 100  # ms


@dataclass
class VehicleState:
    speed_kph: float = 0.0
    target_speed_kph: float = 0.0
    rpm: int = 0
    soc_percent: int = 90
    enabled: bool = False


@dataclass
class EmulatorConfig:
    ids: IdConfig = field(default_factory=IdConfig)
    periods_ms: PeriodsConfig = field(default_factory=PeriodsConfig)
    initial_state: VehicleState = field(default_factory=VehicleState)

    @staticmethod
    def from_yaml(path: Path) -> "EmulatorConfig":
        with path.open("r", encoding="utf-8") as f:
            data = yaml.safe_load(f) or {}
        ids = data.get("ids", {})
        periods = data.get("periods_ms", {})
        initial = data.get("initial_state", {})
        return EmulatorConfig(
            ids=IdConfig(
                speed_feedback=int(ids.get("speed_feedback", 0x100)),
                rpm_feedback=int(ids.get("rpm_feedback", 0x101)),
                soc_feedback=int(ids.get("soc_feedback", 0x102)),
                target_speed_command=int(ids.get("target_speed_command", 0x200)),
                enable_command=int(ids.get("enable_command", 0x201)),
            ),
            periods_ms=PeriodsConfig(
                tx_feedback=int(periods.get("tx_feedback", 100)),
            ),
            initial_state=VehicleState(
                speed_kph=float(initial.get("speed_kph", 0.0)),
                target_speed_kph=float(initial.get("target_speed_kph", 0.0)),
                rpm=int(initial.get("rpm", 0)),
                soc_percent=int(initial.get("soc_percent", 90)),
                enabled=bool(initial.get("enabled", False)),
            ),
        )


class EmulatorRuntime:
    def __init__(
        self,
        interface: str,
        channel: str,
        tx_period_ms: int,
        config: Optional[EmulatorConfig],
        enable_self_test: bool,
    ) -> None:
        self.config = config or EmulatorConfig()
        # override period from CLI if provided
        self.config.periods_ms.tx_feedback = int(tx_period_ms)
        self.state = self.config.initial_state
        self.enable_self_test = enable_self_test

        self.interface = interface
        self.channel = channel
        if interface == "virtual":
            self.bus = can.Bus(interface="virtual", channel=channel, receive_own_messages=True)
        else:
            self.bus = can.Bus(interface="socketcan", channel=channel, receive_own_messages=False)

        self._stop_event = threading.Event()
        self._rx_thread = threading.Thread(target=self._rx_loop, name="can-rx", daemon=True)
        self._tx_thread = threading.Thread(target=self._tx_loop, name="can-tx", daemon=True)
        self._self_test_thread: Optional[threading.Thread] = None

    # === Encoding helpers ===
    @staticmethod
    def _encode_speed(speed_kph: float) -> bytes:
        value = max(0, min(65535, int(round(speed_kph * 10.0))))
        return struct.pack("<H", value)

    @staticmethod
    def _encode_rpm(rpm: int) -> bytes:
        value = max(0, min(65535, int(rpm)))
        return struct.pack("<H", value)

    @staticmethod
    def _encode_soc(soc_percent: int) -> bytes:
        value = max(0, min(100, int(soc_percent)))
        return struct.pack("<B", value)

    # === Dynamics model ===
    def _update_model(self, dt_seconds: float) -> None:
        if not self.state.enabled:
            # passive decay of speed
            self.state.speed_kph = max(0.0, self.state.speed_kph - 0.5 * dt_seconds)
        else:
            # approach target speed with fixed acceleration rate
            accel_kph_per_s = 5.0
            speed_error = self.state.target_speed_kph - self.state.speed_kph
            delta = max(-accel_kph_per_s * dt_seconds, min(accel_kph_per_s * dt_seconds, speed_error))
            self.state.speed_kph += delta

        # compute RPM as linear function of speed (placeholder)
        self.state.rpm = int(self.state.speed_kph * 50.0)

        # simple SoC drain proportional to speed
        drain_per_s = 0.0005 * self.state.speed_kph
        self.state.soc_percent = max(0, min(100, int(self.state.soc_percent - drain_per_s)))

    # === TX/RX loops ===
    def _tx_loop(self) -> None:
        period_s = self.config.periods_ms.tx_feedback / 1000.0
        last = time.monotonic()
        while not self._stop_event.is_set():
            now = time.monotonic()
            dt = now - last
            last = now
            self._update_model(dt)

            # send feedback frames
            frames = [
                can.Message(arbitration_id=self.config.ids.speed_feedback, is_extended_id=False, data=self._encode_speed(self.state.speed_kph))
            ]
            frames.append(
                can.Message(arbitration_id=self.config.ids.rpm_feedback, is_extended_id=False, data=self._encode_rpm(self.state.rpm))
            )
            frames.append(
                can.Message(arbitration_id=self.config.ids.soc_feedback, is_extended_id=False, data=self._encode_soc(self.state.soc_percent))
            )

            for frame in frames:
                try:
                    self.bus.send(frame, timeout=0.05)
                except can.CanError as exc:
                    logger.warning("TX failed: %s", exc)

            self._stop_event.wait(period_s)

    def _rx_loop(self) -> None:
        while not self._stop_event.is_set():
            try:
                msg = self.bus.recv(timeout=0.2)
            except can.CanError as exc:
                logger.warning("RX error: %s", exc)
                continue

            if msg is None:
                continue

            if msg.is_error_frame or msg.is_remote_frame:
                continue

            try:
                self._handle_rx(msg)
            except Exception as exc:  # noqa: BLE001
                logger.exception("Error handling RX frame: %s", exc)

    def _handle_rx(self, msg: can.Message) -> None:
        if msg.arbitration_id == self.config.ids.target_speed_command and len(msg.data) >= 2:
            (raw_speed_01kph,) = struct.unpack_from("<H", msg.data, 0)
            self.state.target_speed_kph = raw_speed_01kph / 10.0
            logger.info("CMD target_speed_kph=%.1f", self.state.target_speed_kph)
            return

        if msg.arbitration_id == self.config.ids.enable_command and len(msg.data) >= 1:
            self.state.enabled = bool(msg.data[0])
            logger.info("CMD enabled=%s", self.state.enabled)
            return

    def _self_test(self) -> None:
        time.sleep(1.0)
        demo_enable = can.Message(
            arbitration_id=self.config.ids.enable_command,
            is_extended_id=False,
            data=bytes([1]),
        )
        demo_speed = can.Message(
            arbitration_id=self.config.ids.target_speed_command,
            is_extended_id=False,
            data=struct.pack("<H", 800),  # 80.0 kph
        )
        try:
            self.bus.send(demo_enable)
            self.bus.send(demo_speed)
            logger.info("Self-test commands injected")
        except can.CanError as exc:
            logger.warning("Self-test TX failed: %s", exc)

    def run(self, duration_seconds: float) -> None:
        logger.info(
            "Starting emulator on interface=%s channel=%s, period=%dms",
            self.interface,
            self.channel,
            self.config.periods_ms.tx_feedback,
        )
        self._rx_thread.start()
        self._tx_thread.start()
        if self.enable_self_test:
            self._self_test_thread = threading.Thread(target=self._self_test, name="self-test", daemon=True)
            self._self_test_thread.start()

        try:
            if duration_seconds and duration_seconds > 0:
                self._stop_event.wait(duration_seconds)
                self.stop()
            else:
                while not self._stop_event.is_set():
                    time.sleep(0.2)
        except KeyboardInterrupt:
            logger.info("Interrupted by user")
            self.stop()

    def stop(self) -> None:
        if self._stop_event.is_set():
            return
        self._stop_event.set()
        logger.info("Stopping emulator...")
        self._rx_thread.join(timeout=2.0)
        self._tx_thread.join(timeout=2.0)
        try:
            self.bus.shutdown()
        except Exception:  # noqa: BLE001
            pass
        logger.info("Emulator stopped")

