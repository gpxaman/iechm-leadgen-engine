"""
Layer 1 -- classification / structured extraction.

In production this is the "Hardware Domain Parser (LLM + Pydantic)" from
the architecture this project is based on: an LLM reads the raw listing and
fills the EngineeringJobSpec-shaped schema. Swapping in a real LLM here is
a one-function change -- see core.llm.LLMClient -- but the default path is
a deterministic keyword/heuristic extractor so the whole pipeline runs with
zero external dependencies and zero API cost.
"""
from __future__ import annotations

import re

from core.models import ChannelType, ClientArchetype, ManufacturingDomain, RawSignal

_CAD_TOOLS = ["Fusion 360", "SolidWorks", "Altium", "KiCad", "AutoCAD", "STEP", "IGES", "Creo", "CATIA"]
_MATERIALS = ["aluminum", "aluminium", "ABS", "PLA", "steel", "stainless steel", "nylon", "PC", "brass", "titanium"]

_DOMAIN_RULES: list[tuple[ManufacturingDomain, re.Pattern]] = [
    (ManufacturingDomain.PCB_ELECTRONICS, re.compile(r"\bpcb\b|\baltium\b|\bkicad\b|\bgerber\b|\bschematic\b|\bfirmware\b|\bembedded\b", re.I)),
    (ManufacturingDomain.DFM_INJECTION_MOLDING, re.compile(r"injection mold|\bdfm\b|draft angle|mold flow|tooling", re.I)),
    (ManufacturingDomain.SHEET_METAL, re.compile(r"sheet metal|stamping|bending|extrusion", re.I)),
    (ManufacturingDomain.PROTOTYPING_3D_PRINT, re.compile(r"3d print|\bsla\b|\bfdm\b|\bsls\b|rapid prototyp", re.I)),
    (ManufacturingDomain.ENCLOSURE_DESIGN, re.compile(r"enclosure|housing|casing", re.I)),
    (ManufacturingDomain.FULL_NPD_TURNKEY, re.compile(r"turnkey|napkin sketch|concept to production|full product development", re.I)),
    (ManufacturingDomain.CAD_MECHANICAL, re.compile(r"\bcad\b|solidworks|fusion ?360|mechanical design|\bstep\b file|\biges\b", re.I)),
]

_STAGE_RULES = [
    (re.compile(r"napkin sketch|just an idea|concept only|early concept", re.I), "Concept / Napkin Sketch"),
    (re.compile(r"working prototype|proof of concept|\bpoc\b", re.I), "Prototype / Proof of Concept"),
    (re.compile(r"ready for production|production ready|need dfm|scale to production", re.I), "Optimization / DFM"),
    (re.compile(r"mass production|high volume|going into production", re.I), "Production Ready"),
]

_ARCHETYPE_BY_CHANNEL = {
    ChannelType.FREELANCE: ClientArchetype.NPD_INNOVATOR,
    ChannelType.B2B_TRADE: ClientArchetype.MIDDLEMAN_RESELLER,
    ChannelType.COMMUNITY: ClientArchetype.NPD_INNOVATOR,
    ChannelType.BROKERAGE: ClientArchetype.SME_ENGINEERING,
    ChannelType.OUTBOUND: ClientArchetype.CROWDFUNDER,
}

_ARCHETYPE_OVERRIDES = [
    (re.compile(r"government|tender|rfp\b|defense|public sector|procurement", re.I), ClientArchetype.INSTITUTIONAL),
    (re.compile(r"reseller|white.?label|oem|odm|import(?:er)?|amazon (?:fba|seller)|shopify brand", re.I), ClientArchetype.MIDDLEMAN_RESELLER),
    (re.compile(r"kickstarter|indiegogo|crowdfund|our backers", re.I), ClientArchetype.CROWDFUNDER),
    (re.compile(r"engineering team|internal team|overflow|bandwidth|fractional engineer", re.I), ClientArchetype.SME_ENGINEERING),
]


def classify_domain(text: str) -> ManufacturingDomain:
    for domain, pattern in _DOMAIN_RULES:
        if pattern.search(text):
            return domain
    return ManufacturingDomain.UNKNOWN


def classify_archetype(signal: RawSignal) -> ClientArchetype:
    text = f"{signal.title}\n{signal.raw_text}"
    for pattern, archetype in _ARCHETYPE_OVERRIDES:
        if pattern.search(text):
            return archetype
    return _ARCHETYPE_BY_CHANNEL.get(signal.channel_type, ClientArchetype.UNKNOWN)


def extract_cad_software(text: str) -> list[str]:
    return sorted({tool for tool in _CAD_TOOLS if re.search(re.escape(tool), text, re.I)})


def extract_materials(text: str) -> list[str]:
    return sorted({m for m in _MATERIALS if re.search(re.escape(m), text, re.I)})


def extract_deliverables(text: str) -> list[str]:
    deliverables = []
    checks = {
        r"\bstep\b file": "STEP file",
        r"\bgerber\b": "Gerber/BOM",
        r"2d (?:production )?drawing": "2D Production Drawings",
        r"\bbom\b": "BOM",
        r"render": "Renders",
        r"moldflow": "Moldflow Analysis",
        r"prototype": "Physical Prototype",
    }
    for pat, label in checks.items():
        if re.search(pat, text, re.I):
            deliverables.append(label)
    return deliverables or ["STEP file", "2D Production Drawings"]


def classify_project_stage(text: str) -> str:
    for pattern, stage in _STAGE_RULES:
        if pattern.search(text):
            return stage
    return "Unspecified"
