"""
Pluggable LLM boundary.

Every stage that would "call an LLM" in the full production architecture
(Writer persuasion, Strategist reasoning, Layer 1 extraction) goes through
an object implementing LLMClient. The default TemplateLLM is pure stdlib
and deterministic, so the whole system runs offline with zero API cost.

To go live: set ANTHROPIC_API_KEY and pass use_anthropic=True to
get_default_llm() (or just construct AnthropicLLM yourself). Nothing else
in the codebase needs to change -- core.writer only ever calls
`llm.complete(prompt)`.
"""
from __future__ import annotations

import os
from typing import Protocol


class LLMClient(Protocol):
    def complete(self, prompt: str, *, max_tokens: int = 400) -> str: ...


class TemplateLLM:
    """Deterministic, zero-dependency stand-in. Good enough to drive the
    whole pipeline end-to-end for demos/tests without any API key."""

    name = "template-llm-v1"

    def complete(self, prompt: str, *, max_tokens: int = 400) -> str:
        # In this offline mode, core.writer builds proposal text itself via
        # templating and never actually needs generative completion -- this
        # stub exists so the interface is real and swappable.
        return prompt[:max_tokens]


class AnthropicLLM:
    """Thin wrapper around the Anthropic SDK. Only imported/instantiated on
    demand so the rest of the system has no hard dependency on it."""

    def __init__(self, model: str = "claude-sonnet-5", api_key: str | None = None):
        try:
            import anthropic  # type: ignore
        except ImportError as exc:  # pragma: no cover
            raise RuntimeError(
                "anthropic package not installed; `pip install anthropic` to use AnthropicLLM"
            ) from exc
        key = api_key or os.environ.get("ANTHROPIC_API_KEY")
        if not key:
            raise RuntimeError("ANTHROPIC_API_KEY not set")
        self._client = anthropic.Anthropic(api_key=key)
        self.model = model
        self.name = f"anthropic:{model}"

    def complete(self, prompt: str, *, max_tokens: int = 400) -> str:
        resp = self._client.messages.create(
            model=self.model,
            max_tokens=max_tokens,
            messages=[{"role": "user", "content": prompt}],
        )
        return "".join(block.text for block in resp.content if getattr(block, "type", "") == "text")


def get_default_llm(use_anthropic: bool = False) -> LLMClient:
    if use_anthropic and os.environ.get("ANTHROPIC_API_KEY"):
        try:
            return AnthropicLLM()
        except RuntimeError:
            pass
    return TemplateLLM()
