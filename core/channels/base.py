"""
Channel adapter contract.

Every lead-source type (freelance marketplace, B2B trade directory,
community/forum, agency brokerage, outbound signal) implements this same
interface. The orchestrator and dynamic agent-spawn logic never know or
care which concrete channel they're driving -- they only call
`list_platforms()` / `sub_domains()` / `generate_daily_signals()`.

`generate_daily_signals()` is a SIMULATOR: it stands in for the real
scraper (Playwright/Firecrawl/platform API) described in the source
architecture. Swapping simulation for a live scraper means writing a new
class that implements this same interface -- nothing else in the system
changes, which is the entire point of the adapter boundary.
"""
from __future__ import annotations

from abc import ABC, abstractmethod

from core.models import ChannelType, RawSignal


class ChannelAdapter(ABC):
    channel_type: ChannelType

    @abstractmethod
    def list_platforms(self) -> list[str]:
        """Concrete platforms this channel type covers, e.g. Upwork, Freelancer.com."""

    @abstractmethod
    def sub_domains(self, platform: str) -> list[str]:
        """Granular sub-channels within a platform (subreddits, RFQ category
        tags, regional listings...). Empty list if the platform has none."""

    @abstractmethod
    def generate_daily_signals(self, day: int) -> list[RawSignal]:
        """One simulated day's worth of raw signals across all platforms/sub-domains
        for this channel. `day` seeds the RNG so runs are reproducible."""

    def friction_level(self) -> str:
        """Anti-scraping / access friction for this channel, used by the
        orchestrator's channel-health scoring. Override per adapter."""
        return "MEDIUM"
