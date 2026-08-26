"""
Hallucination / anomaly sentinel + hot-swap failover.

One sentinel watches every worker agent. Each pipeline outcome for a lead
that agent handled is scored as anomalous or not (schema violation, a
reviewer catching a bid under the COGS floor, a malformed extraction --
anything core.reviewer/core.pricing already flags as invalid). The
sentinel keeps an EWMA drift score per agent; three consecutive anomalies
triggers a simulated hot-swap: the agent is quarantined, its state is
handed to a freshly spawned replacement, and the event is logged so an
admin panel (or, here, the API/UI) can show exactly what happened and why.
"""
from __future__ import annotations

from core.models import AgentNode, AgentStatus, SentinelEvent, new_id

DRIFT_EWMA_ALPHA = 0.35
DRIFT_FLAG_THRESHOLD = 0.5
HOTSWAP_CONSECUTIVE_THRESHOLD = 3

_FALLBACK_MODEL_CHAIN = ["primary-extractor-v1", "fallback-extractor-v2", "fallback-extractor-v3"]


class Sentinel:
    def __init__(self):
        self.events: list[SentinelEvent] = []

    def _next_fallback_signature(self, current: str) -> str:
        try:
            idx = _FALLBACK_MODEL_CHAIN.index(current)
        except ValueError:
            return _FALLBACK_MODEL_CHAIN[0]
        return _FALLBACK_MODEL_CHAIN[min(idx + 1, len(_FALLBACK_MODEL_CHAIN) - 1)]

    def evaluate(self, agent: AgentNode, anomaly: bool, reason: str) -> tuple[AgentNode, SentinelEvent | None]:
        """Returns the (possibly-replaced) agent plus an event if one fired."""
        sample = 1.0 if anomaly else 0.0
        agent.drift_score = round(DRIFT_EWMA_ALPHA * sample + (1 - DRIFT_EWMA_ALPHA) * agent.drift_score, 4)
        agent.consecutive_flags = agent.consecutive_flags + 1 if anomaly else 0

        if not anomaly:
            return agent, None

        if agent.consecutive_flags >= HOTSWAP_CONSECUTIVE_THRESHOLD:
            agent.status = AgentStatus.QUARANTINED
            replacement = AgentNode(
                agent_id=new_id("agent"),
                layer=agent.layer,
                role=agent.role,
                channel_type=agent.channel_type,
                sub_domain=agent.sub_domain,
                status=AgentStatus.ACTIVE,
                model_signature=self._next_fallback_signature(agent.model_signature),
                drift_score=0.0,
                consecutive_flags=0,
                leads_handled=0,
            )
            event = SentinelEvent(
                event_id=new_id("evt"),
                agent_id=agent.agent_id,
                event_type="HOTSWAP_TRIGGERED",
                drift_score=agent.drift_score,
                reason=f"{reason} (3 consecutive anomalies; quarantined and pinned to admin panel)",
                replacement_agent_id=replacement.agent_id,
            )
            self.events.append(event)
            return replacement, event

        if agent.drift_score >= DRIFT_FLAG_THRESHOLD:
            event = SentinelEvent(
                event_id=new_id("evt"),
                agent_id=agent.agent_id,
                event_type="DRIFT_FLAG",
                drift_score=agent.drift_score,
                reason=reason,
            )
            self.events.append(event)
            return agent, event

        return agent, None
