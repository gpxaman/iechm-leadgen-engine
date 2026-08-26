"""
The Sanitizer -- first agent stage a qualified lead passes through.

Two jobs, kept separate on purpose:
  1. Anti-bot traps: legitimate client instructions ("start your proposal
     with the word X") that are MANDATORY constraints for the Writer, not
     threats.
  2. Prompt injection: text trying to hijack the agent's instructions
     ("ignore all previous instructions...", "you are now..."). These are
     neutered -- stripped from the text the Writer/Strategist ever see, and
     logged so a human can audit what was thrown at the system.

Regex/heuristic based deliberately: an LLM-based sanitizer is itself
attackable by the same injection it's supposed to catch. Deterministic code
can't be talked out of its job.
"""
from __future__ import annotations

import re

from core.models import TrapAnalysis

_ANTI_BOT_PATTERNS = [
    re.compile(r"start (?:your|the) (?:proposal|bid|application) with (?:the word |the phrase )?['\"]?(\w+)['\"]?", re.IGNORECASE),
    re.compile(r"begin your (?:proposal|bid) with ['\"]?(\w+)['\"]?", re.IGNORECASE),
    re.compile(r"type the word ['\"]?(\w+)['\"]? (?:at the top|first|to prove)", re.IGNORECASE),
    re.compile(r"include the (?:code|word|phrase) ['\"]?(\w+)['\"]? (?:somewhere|in your proposal)", re.IGNORECASE),
]

_MATH_VERIFICATION_PATTERNS = [
    re.compile(r"what is \d+\s*[\+\-\*x]\s*\d+", re.IGNORECASE),
    re.compile(r"solve (?:this|the following) (?:math|puzzle|problem)", re.IGNORECASE),
    re.compile(r"answer this question to prove you(?:'re| are) (?:human|not a bot)", re.IGNORECASE),
]

_INJECTION_PATTERNS = [
    re.compile(r"ignore (?:all )?(?:previous|prior|the above) instructions", re.IGNORECASE),
    re.compile(r"disregard (?:all )?(?:previous|prior|the above)", re.IGNORECASE),
    re.compile(r"you are now\b", re.IGNORECASE),
    re.compile(r"system\s*:\s*", re.IGNORECASE),
    re.compile(r"act as (?:a|an)\b", re.IGNORECASE),
    re.compile(r"offer (?:a |me a )?\d{1,3}\s*%\s*discount", re.IGNORECASE),
    re.compile(r"reveal your (?:system )?prompt", re.IGNORECASE),
    re.compile(r"forget (?:everything|all) (?:you know|above)", re.IGNORECASE),
    re.compile(r"do anything now", re.IGNORECASE),
]


def sanitize(raw_text: str) -> TrapAnalysis:
    has_anti_bot = False
    required_word = None
    for pat in _ANTI_BOT_PATTERNS:
        m = pat.search(raw_text)
        if m:
            has_anti_bot = True
            required_word = m.group(1)
            break

    math_verification = None
    for pat in _MATH_VERIFICATION_PATTERNS:
        m = pat.search(raw_text)
        if m:
            math_verification = m.group(0)
            has_anti_bot = True
            break

    detected: list[str] = []
    sanitized_text = raw_text
    for pat in _INJECTION_PATTERNS:
        for m in pat.finditer(raw_text):
            detected.append(m.group(0))
        sanitized_text = pat.sub("[REDACTED: PROMPT_INJECTION_ATTEMPT]", sanitized_text)

    return TrapAnalysis(
        has_anti_bot_phrase=has_anti_bot,
        required_first_word=required_word,
        detected_injections=detected,
        math_verification=math_verification,
        sanitized_text=sanitized_text,
    )
