"""Global B2B sourcing & trade directories: Alibaba, Made-in-China, Global Sources, IndiaMART."""
from __future__ import annotations

from core.channels.base import ChannelAdapter
from core.channels._util import make_signal, maybe_inject_trap, rng
from core.models import ChannelType, RawSignal

_PLATFORMS = ["Alibaba", "Made-in-China", "Global Sources", "IndiaMART"]

_RFQ_TEMPLATES = [
    ("Custom injection-molded ABS phone stand, need mold + DFM", 10000, 8500),
    ("OEM redesign of existing Bluetooth speaker housing with our logo", 5000, 6200),
    ("CNC machined aluminum heatsinks, need 2D drawings reviewed", 50000, 22000),
    ("Sheet metal enclosure for industrial control panel, 500 units", 500, 9000),
    ("PCB assembly (PCBA) for existing board design, 2000 units", 2000, 14000),
    ("Full tooling for plastic clip, private label packaging", 20000, 15000),
    ("Aluminum extrusion profile for LED fixture, custom length", 8000, 7000),
]


class B2BTradeAdapter(ChannelAdapter):
    channel_type = ChannelType.B2B_TRADE

    def list_platforms(self) -> list[str]:
        return list(_PLATFORMS)

    def sub_domains(self, platform: str) -> list[str]:
        return ["RFQ:Tooling", "RFQ:PCBA", "RFQ:Sheet_Metal", "RFQ:Component_Sourcing"]

    def friction_level(self) -> str:
        return "HIGH"

    def generate_daily_signals(self, day: int) -> list[RawSignal]:
        signals: list[RawSignal] = []
        for platform in _PLATFORMS:
            r = rng(day, f"b2b:{platform}")
            n = r.randint(5, 12)
            for i in range(n):
                title, volume, base_budget = r.choice(_RFQ_TEMPLATES)
                volume = int(volume * r.uniform(0.5, 1.5))
                budget = round(base_budget * r.uniform(0.5, 1.4), 2)
                # occasionally simulate a mathematically-impossible RFQ (spam / price-fishing)
                if r.random() < 0.1:
                    budget = round(volume * 0.005, 2)
                text = (
                    f"RFQ: {title}. Target MOQ {volume} units. Budget ceiling ${budget:.0f} total. "
                    f"Please quote unit price, lead time, and tooling cost separately."
                )
                text = maybe_inject_trap(r, text, trap_rate=0.05)
                sub = r.choice(self.sub_domains(platform))
                signals.append(make_signal(self.channel_type, platform, sub, title, text, budget, volume, f"rfq-{day}-{i}"))
        return signals
