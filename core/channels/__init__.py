from core.channels.b2b_trade import B2BTradeAdapter
from core.channels.base import ChannelAdapter
from core.channels.brokerage import BrokerageAdapter
from core.channels.community import CommunityAdapter
from core.channels.freelance import FreelanceAdapter
from core.channels.outbound import OutboundAdapter
from core.models import ChannelType

REGISTRY: dict[ChannelType, ChannelAdapter] = {
    ChannelType.FREELANCE: FreelanceAdapter(),
    ChannelType.B2B_TRADE: B2BTradeAdapter(),
    ChannelType.COMMUNITY: CommunityAdapter(),
    ChannelType.BROKERAGE: BrokerageAdapter(),
    ChannelType.OUTBOUND: OutboundAdapter(),
}


def all_adapters() -> list[ChannelAdapter]:
    return list(REGISTRY.values())


__all__ = ["ChannelAdapter", "REGISTRY", "all_adapters"]
