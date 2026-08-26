"""Outbound signals: crowdfunding campaigns (Kickstarter/Indiegogo) and
patent filings (USPTO/WIPO) with validated demand but no active RFQ --
these need cold outreach, not a bid."""
from __future__ import annotations

from core.channels.base import ChannelAdapter
from core.channels._util import make_signal, maybe_inject_trap, rng
from core.models import ChannelType, RawSignal

_PLATFORMS = ["Kickstarter", "Indiegogo", "USPTO Patent Filings"]

_SIGNAL_TEMPLATES = [
    "Funded 140% on our smart-home hub campaign, but our CAD is still just a 3D render, no DFM done yet, worried about our July ship date.",
    "Update #4: manufacturing delay, our injection mold supplier rejected our files for zero draft angle, need to redesign fast.",
    "New utility patent published for a modular enclosure system, no visible commercial manufacturing partner yet.",
    "Hit our Indiegogo goal for a wearable device, need to go from prototype PCB to production-ready Gerber files.",
    "Patent filing describes a novel sheet-metal bracket mechanism, inventor appears to be a first-time hardware founder.",
]


class OutboundAdapter(ChannelAdapter):
    channel_type = ChannelType.OUTBOUND

    def list_platforms(self) -> list[str]:
        return list(_PLATFORMS)

    def sub_domains(self, platform: str) -> list[str]:
        if platform in ("Kickstarter", "Indiegogo"):
            return ["Technology", "Design", "Gadgets"]
        return ["Utility Patents", "Design Patents"]

    def friction_level(self) -> str:
        return "MEDIUM"

    def generate_daily_signals(self, day: int) -> list[RawSignal]:
        signals: list[RawSignal] = []
        for platform in _PLATFORMS:
            r = rng(day, f"outbound:{platform}")
            n = r.randint(2, 6)
            for i in range(n):
                text = r.choice(_SIGNAL_TEMPLATES)
                text = maybe_inject_trap(r, text, trap_rate=0.02)
                sub = r.choice(self.sub_domains(platform))
                # outbound signals rarely state a budget -- that's the point,
                # it's inferred from funding raised, not a stated ask
                signals.append(make_signal(self.channel_type, platform, sub, text[:65], text, None, None, f"signal-{day}-{i}"))
        return signals
