import argparse
import logging
from pathlib import Path
from typing import Optional

from .runtime import EmulatorRuntime, EmulatorConfig


def _configure_logging(level: str) -> None:
    numeric = getattr(logging, level.upper(), logging.INFO)
    logging.basicConfig(
        level=numeric,
        format="%(asctime)s %(levelname)s %(name)s - %(message)s",
    )


def _load_config(path: Optional[str]) -> Optional[EmulatorConfig]:
    if not path:
        return None
    config_path = Path(path)
    if not config_path.exists():
        raise FileNotFoundError(f"Config file not found: {config_path}")
    return EmulatorConfig.from_yaml(config_path)


def cli() -> None:
    parser = argparse.ArgumentParser(
        prog="can_emulator",
        description="Simple CAN bus emulator for VCU testing",
    )

    subparsers = parser.add_subparsers(dest="command", required=True)

    run_parser = subparsers.add_parser("run", help="Run the emulator")
    run_parser.add_argument("--channel", default="vcan0", help="CAN channel (e.g. vcan0)")
    run_parser.add_argument(
        "--interface",
        default="socketcan",
        choices=["socketcan", "virtual"],
        help="CAN interface backend",
    )
    run_parser.add_argument("--tx-period-ms", type=int, default=100, help="Feedback TX period in ms")
    run_parser.add_argument("--duration", type=float, default=0.0, help="Stop after N seconds (0=run until Ctrl+C)")
    run_parser.add_argument("--log-level", default="INFO", help="Logging level")
    run_parser.add_argument("--config", default=None, help="YAML config path to override defaults")
    run_parser.add_argument("--self-test", action="store_true", help="Inject a demo command after 1s")

    args = parser.parse_args()

    if args.command == "run":
        _configure_logging(args.log_level)
        config = _load_config(args.config)
        runtime = EmulatorRuntime(
            interface=args.interface,
            channel=args.channel,
            tx_period_ms=args.tx_period_ms,
            config=config,
            enable_self_test=args.self_test,
        )
        runtime.run(duration_seconds=args.duration)


if __name__ == "__main__":
    cli()

