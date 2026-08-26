"""Freelance & work-for-hire marketplaces: Upwork, Freelancer.com, Cad Crowd, Guru."""
from __future__ import annotations

from core.channels.base import ChannelAdapter
from core.channels._util import BLACKLISTED_FILLER, make_signal, maybe_inject_trap, rng
from core.models import ChannelType, RawSignal

_PLATFORMS = ["Upwork", "Freelancer.com", "Cad Crowd", "Guru"]

_TITLES = [
    ("Need SolidWorks CAD model for a handheld device enclosure", 450, None),
    ("PCB layout for IoT sensor board (4-layer, Altium)", 900, None),
    ("Fusion 360 redesign of existing bracket for CNC machining", 250, None),
    ("DFM review + injection mold draft angles for plastic housing", 1200, None),
    ("KiCad schematic + Gerber files for a motor controller", 700, None),
    ("3D print prototype of a wearable clip, need STL + STEP", 180, None),
    ("Full NPD: napkin sketch to production-ready CAD for a kitchen gadget", 4500, None),
    ("Sheet metal enclosure design for server rack accessory", 650, None),
    ("Quick CAD conversion: hand sketch to STEP file", 60, None),
    ("Need help with tolerance stack-up analysis on an assembly", 300, None),
]


class FreelanceAdapter(ChannelAdapter):
    channel_type = ChannelType.FREELANCE

    def list_platforms(self) -> list[str]:
        return list(_PLATFORMS)

    def sub_domains(self, platform: str) -> list[str]:
        return []  # freelance boards are flat category listings, no sub-domain spawn tier

    def friction_level(self) -> str:
        return "LOW"

    def generate_daily_signals(self, day: int) -> list[RawSignal]:
        signals: list[RawSignal] = []
        for platform in _PLATFORMS:
            r = rng(day, f"freelance:{platform}")
            n = r.randint(4, 9)
            for i in range(n):
                if r.random() < 0.08:
                    text = r.choice(BLACKLISTED_FILLER)
                    signals.append(make_signal(self.channel_type, platform, "", text[:60], text, r.choice([50, 100, 200]), None, f"job-{day}-{i}"))
                    continue
                title, base_budget, volume = r.choice(_TITLES)
                budget = round(base_budget * r.uniform(0.6, 1.6), 2)
                text = f"{title}. Budget range approx ${budget:.0f}. Looking for someone who can start this week."
                text = maybe_inject_trap(r, text)
                signals.append(make_signal(self.channel_type, platform, "", title, text, budget, volume, f"job-{day}-{i}"))
        return signals
