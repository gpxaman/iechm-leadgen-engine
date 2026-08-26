"""Developer/community forums: Reddit subreddits, Discord, Hackaday.

This is the channel type where the "5-leads/day -> spawn a dedicated
sub-domain agent" rule actually matters, since volume varies wildly by
sub-domain (a hot subreddit vs. a quiet one).
"""
from __future__ import annotations

from core.channels.base import ChannelAdapter
from core.channels._util import make_signal, maybe_inject_trap, rng
from core.models import ChannelType, RawSignal

_PLATFORMS = ["Reddit", "Hackaday", "Discord (Hardware Servers)"]

# (sub_domain, avg_leads_per_day) -- deliberately uneven so some subs cross
# the 5-lead/day spawn threshold and others stay quiet.
_SUBS = {
    "Reddit": [
        ("r/HardwareStartups", 6),
        ("r/PrintedCircuitBoard", 8),
        ("r/CAD", 3),
        ("r/Machinists", 2),
        ("r/injectionmolding", 4),
        ("r/AskElectronics", 7),
    ],
    "Hackaday": [("io.hackaday.com/projects", 3)],
    "Discord (Hardware Servers)": [("#hardware-help", 4), ("#pcb-review", 2)],
}

_POST_TEMPLATES = [
    "Just got my first 3D printed prototype back and the walls are way too thin, how do I fix draft angles for injection molding later?",
    "Posting my PCB layout (KiCad) for review before I send it to fab, any glaring EMC issues?",
    "We hit our Kickstarter goal, now realizing our CAD isn't manufacturable at all. Where do people usually go for DFM help?",
    "CNC quote came back 3x higher than expected for this bracket, is my design just bad for machining?",
    "Trying to go from SolidWorks model to actual injection mold, no idea where to start with tooling cost.",
    "Anyone recommend someone who does full turnkey NPD? Have a napkin sketch and a bit of funding.",
]


class CommunityAdapter(ChannelAdapter):
    channel_type = ChannelType.COMMUNITY

    def list_platforms(self) -> list[str]:
        return list(_PLATFORMS)

    def sub_domains(self, platform: str) -> list[str]:
        return [s for s, _ in _SUBS.get(platform, [])]

    def friction_level(self) -> str:
        return "LOW"

    def generate_daily_signals(self, day: int) -> list[RawSignal]:
        signals: list[RawSignal] = []
        for platform, subs in _SUBS.items():
            for sub_domain, avg in subs:
                r = rng(day, f"community:{platform}:{sub_domain}")
                # poisson-ish jitter around the average without needing numpy
                n = max(0, round(avg + r.uniform(-2, 3)))
                for i in range(n):
                    text = r.choice(_POST_TEMPLATES)
                    text = maybe_inject_trap(r, text, trap_rate=0.03)
                    budget = None if r.random() < 0.7 else round(r.uniform(100, 2000), 2)
                    signals.append(make_signal(
                        self.channel_type, platform, sub_domain,
                        text[:70], text, budget, None, f"post-{day}-{sub_domain}-{i}".replace("/", "_").replace("#", "")
                    ))
        return signals
