# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

"""Typed data model for the bindless codegen pipeline."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional


@dataclass
class Meta:
    version: Optional[str] = None
    schema_version: Optional[str] = None
    description: Optional[str] = None
    source: Optional[str] = None


@dataclass
class Defaults:
    invalid_index: int = 0xFFFFFFFF


@dataclass
class Domain:
    id: str
    name: str
    kind: str
    domain_base: int
    capacity: int
    register: Optional[str] = None
    space: Optional[str] = None
    root_table: Optional[str] = None
    comment: Optional[str] = None
    uav_counter_register: Optional[str] = None


@dataclass
class Range:
    range_type: str
    domains: List[str] = field(default_factory=list)
    base_shader_register: Optional[str] = None
    register_space: Optional[str] = None
    num_descriptors: Any = None  # int or "unbounded"


@dataclass
class RootParameter:
    type: str
    name: str
    index: int
    visibility: List[str]
    ranges: List[Range] = field(default_factory=list)
    shader_register: Optional[str] = None
    register_space: Optional[str] = None
    cbv_array_size: Optional[int] = None
    num_32bit_values: Optional[int] = None
    domains: List[str] = field(default_factory=list)


@dataclass
class Heap:
    id: str
    type: str
    shader_visible: bool
    capacity: int
    base_index: int
    allow_growth: bool = False
    debug_name: Optional[str] = None


@dataclass
class Mapping:
    domain: str
    heap: str
    local_base: int


@dataclass
class Model:
    meta: Meta
    defaults: Defaults
    domains: List[Domain]
    root_signature: List[RootParameter]
    heaps: List[Heap]
    mappings: List[Mapping]
    symbols: Dict[str, Any]


def _coerce_int(val: Any) -> Optional[int]:
    if val is None:
        return None
    if isinstance(val, int):
        return val
    try:
        return int(val)
    except Exception:
        return None


def build_model(doc: Dict[str, Any]) -> Model:
    meta = Meta(**(doc.get("meta") or {}))
    defaults = Defaults(**(doc.get("defaults") or {}))

    domains: List[Domain] = []
    for d in doc.get("domains") or []:
        domains.append(
            Domain(
                id=d.get("id"),
                name=d.get("name"),
                kind=d.get("kind"),
                domain_base=int(d.get("domain_base")),
                capacity=int(d.get("capacity")),
                register=d.get("register"),
                space=d.get("space"),
                root_table=d.get("root_table"),
                comment=d.get("comment"),
                uav_counter_register=d.get("uav_counter_register"),
            )
        )

    rs: List[RootParameter] = []
    for p in doc.get("root_signature") or []:
        ranges: List[Range] = []
        for r in p.get("ranges") or []:
            ranges.append(
                Range(
                    range_type=r.get("range_type"),
                    domains=list(r.get("domain") or []),
                    base_shader_register=r.get("base_shader_register"),
                    register_space=r.get("register_space"),
                    num_descriptors=r.get("num_descriptors"),
                )
            )
        rp = RootParameter(
            type=p.get("type"),
            name=p.get("name"),
            index=int(p.get("index")),
            visibility=list(p.get("visibility") or []),
            ranges=ranges,
            shader_register=p.get("shader_register"),
            register_space=p.get("register_space"),
            cbv_array_size=_coerce_int(p.get("cbv_array_size")),
            num_32bit_values=_coerce_int(p.get("num_32bit_values")),
            domains=list(p.get("domains") or []),
        )
        rs.append(rp)

    heaps: List[Heap] = []
    for h in doc.get("heaps") or []:
        heaps.append(
            Heap(
                id=h.get("id"),
                type=h.get("type"),
                shader_visible=bool(h.get("shader_visible")),
                capacity=int(h.get("capacity")),
                base_index=int(h.get("base_index")),
                allow_growth=bool(h.get("allow_growth", False)),
                debug_name=h.get("debug_name"),
            )
        )

    mappings: List[Mapping] = []
    for m in doc.get("mappings") or []:
        mappings.append(
            Mapping(
                domain=m.get("domain"),
                heap=m.get("heap"),
                local_base=int(m.get("local_base", 0)),
            )
        )

    symbols = dict(doc.get("symbols") or {})

    return Model(
        meta=meta,
        defaults=defaults,
        domains=domains,
        root_signature=rs,
        heaps=heaps,
        mappings=mappings,
        symbols=symbols,
    )
