"""Agency matchmaking & NPD brokerages: Clutch.co, DesignRush, Gembah."""
from __future__ import annotations

from core.channels.base import ChannelAdapter
from core.channels._util import make_signal, maybe_inject_trap, rng
from core.models import ChannelType, RawSignal

_PLATFORMS = ["Clutch.co", "DesignRush", "Gembah"]

_RFP_TEMPLATES = [
    ("Enterprise RFP: full mechanical + electrical engineering for IoT product line", 45000),
    ("Seeking vetted CAD/DFM partner for consumer appliance refresh, formal SOW required", 28000),
    ("Fractional PCB engineering team needed for 6-month embedded systems overflow", 60000),
    ("Full NPD partner for medtech-adjacent enclosure, NDA required before brief release", 90000),
    ("Sheet metal + tooling engineering RFP for industrial equipment manufacturer", 35000),
]


class BrokerageAdapter(ChannelAdapter):
    channel_type = ChannelType.BROKERAGE

    def list_platforms(self) -> list[str]:
        return list(_PLATFORMS)

    def sub_domains(self, platform: str) -> list[str]:
        return []

    def friction_level(self) -> str:
        return "HIGH"  # vetting/approval gated, not scraping-gated

    def generate_daily_signals(self, day: int) -> list[RawSignal]:
        signals: list[RawSignal] = []
        for platform in _PLATFORMS:
            r = rng(day, f"brokerage:{platform}")
            n = r.randint(0, 3)  # low volume, high value -- matches the source discussion
            for i in range(n):
                title, base_budget = r.choice(_RFP_TEMPLATES)
                budget = round(base_budget * r.uniform(0.7, 1.5), 2)
                text = (
                    f"{title}. Formal RFP submitted via matchmaking portal. Budget band ${budget:,.0f}. "
                    f"Vetted partners only; case studies required with response."
                )
                text = maybe_inject_trap(r, text, trap_rate=0.04)
                signals.append(make_signal(self.channel_type, platform, "", title, text, budget, None, f"rfp-{day}-{i}"))
        return signals
