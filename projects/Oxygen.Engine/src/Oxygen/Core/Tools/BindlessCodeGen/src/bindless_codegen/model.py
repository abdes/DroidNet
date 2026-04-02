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
class IndexSpace:
    id: str


@dataclass
class AbiDomain:
    id: str
    name: str
    index_space: str
    shader_index_base: int
    capacity: int
    shader_access_class: str
    view_types: List[str] = field(default_factory=list)
    comment: Optional[str] = None


@dataclass
class D3D12Heap:
    id: str
    type: str
    shader_visible: bool
    capacity: int
    base_index: int
    allow_growth: bool = False
    growth_factor: Optional[float] = None
    max_growth_iterations: Optional[int] = None
    debug_name: Optional[str] = None


@dataclass
class D3D12Table:
    id: str
    descriptor_kind: str
    heap: str
    shader_register: str
    register_space: str
    unbounded: bool = False
    descriptor_count: Optional[int] = None


@dataclass
class D3D12DomainRealization:
    domain: str
    table: str
    heap_local_base: int


@dataclass
class D3D12RootParameter:
    type: str
    id: str
    index: int
    visibility: List[str]
    table: Optional[str] = None
    shader_register: Optional[str] = None
    register_space: Optional[str] = None
    num_32bit_values: Optional[int] = None


@dataclass
class VulkanDescriptorSet:
    id: str
    set: int


@dataclass
class VulkanBinding:
    id: str
    set: str
    binding: int
    descriptor_type: str
    descriptor_count: Optional[int] = None
    variable_count: bool = False
    update_after_bind: bool = False


@dataclass
class VulkanDomainRealization:
    domain: str
    binding: str
    array_element_base: int


@dataclass
class VulkanPipelineEntry:
    type: str
    id: str
    set_ref: Optional[str] = None
    size_bytes: Optional[int] = None
    stages: List[str] = field(default_factory=list)


@dataclass
class D3D12Backend:
    heaps: List[D3D12Heap] = field(default_factory=list)
    tables: List[D3D12Table] = field(default_factory=list)
    domain_realizations: List[D3D12DomainRealization] = field(default_factory=list)
    root_signature: List[D3D12RootParameter] = field(default_factory=list)


@dataclass
class VulkanBackend:
    descriptor_sets: List[VulkanDescriptorSet] = field(default_factory=list)
    bindings: List[VulkanBinding] = field(default_factory=list)
    domain_realizations: List[VulkanDomainRealization] = field(default_factory=list)
    pipeline_layout: List[VulkanPipelineEntry] = field(default_factory=list)


@dataclass
class Model:
    meta: Meta
    defaults: Defaults
    index_spaces: List[IndexSpace]
    abi_domains: List[AbiDomain]
    d3d12: D3D12Backend
    vulkan: VulkanBackend


def _coerce_int(val: Any) -> Optional[int]:
    if val is None:
        return None
    if isinstance(val, int):
        return val
    try:
        return int(val)
    except Exception:
        return None


def _coerce_float(val: Any) -> Optional[float]:
    if val is None:
        return None
    if isinstance(val, float):
        return val
    try:
        return float(val)
    except Exception:
        return None


def build_model(doc: Dict[str, Any]) -> Model:
    meta = Meta(**(doc.get("meta") or {}))
    defaults = Defaults(**(doc.get("defaults") or {}))

    index_spaces: List[IndexSpace] = []
    for index_space in ((doc.get("abi") or {}).get("index_spaces") or []):
        index_spaces.append(IndexSpace(id=str(index_space.get("id"))))

    abi_domains: List[AbiDomain] = []
    for domain in ((doc.get("abi") or {}).get("domains") or []):
        abi_domains.append(
            AbiDomain(
                id=str(domain.get("id")),
                name=str(domain.get("name")),
                index_space=str(domain.get("index_space")),
                shader_index_base=int(domain.get("shader_index_base")),
                capacity=int(domain.get("capacity")),
                shader_access_class=str(domain.get("shader_access_class")),
                view_types=list(domain.get("view_types") or []),
                comment=domain.get("comment"),
            )
        )

    d3d12_doc = ((doc.get("backends") or {}).get("d3d12") or {})
    d3d12_strategy = d3d12_doc.get("strategy") or {}
    d3d12_heaps: List[D3D12Heap] = []
    for heap in d3d12_strategy.get("heaps") or []:
        d3d12_heaps.append(
            D3D12Heap(
                id=str(heap.get("id")),
                type=str(heap.get("type")),
                shader_visible=bool(heap.get("shader_visible")),
                capacity=int(heap.get("capacity")),
                base_index=int(heap.get("base_index")),
                allow_growth=bool(heap.get("allow_growth", False)),
                growth_factor=_coerce_float(heap.get("growth_factor")),
                max_growth_iterations=_coerce_int(
                    heap.get("max_growth_iterations")
                ),
                debug_name=heap.get("debug_name"),
            )
        )

    d3d12_tables: List[D3D12Table] = []
    for table in d3d12_strategy.get("tables") or []:
        d3d12_tables.append(
            D3D12Table(
                id=str(table.get("id")),
                descriptor_kind=str(table.get("descriptor_kind")),
                heap=str(table.get("heap")),
                shader_register=str(table.get("shader_register")),
                register_space=str(table.get("register_space")),
                unbounded=bool(table.get("unbounded", False)),
                descriptor_count=_coerce_int(table.get("descriptor_count")),
            )
        )

    d3d12_realizations: List[D3D12DomainRealization] = []
    for realization in d3d12_strategy.get("domain_realizations") or []:
        d3d12_realizations.append(
            D3D12DomainRealization(
                domain=str(realization.get("domain")),
                table=str(realization.get("table")),
                heap_local_base=int(realization.get("heap_local_base")),
            )
        )

    d3d12_root_signature: List[D3D12RootParameter] = []
    for param in d3d12_doc.get("root_signature") or []:
        d3d12_root_signature.append(
            D3D12RootParameter(
                type=str(param.get("type")),
                id=str(param.get("id")),
                index=int(param.get("index")),
                visibility=list(param.get("visibility") or []),
                table=param.get("table"),
                shader_register=param.get("shader_register"),
                register_space=param.get("register_space"),
                num_32bit_values=_coerce_int(param.get("num_32bit_values")),
            )
        )

    vulkan_doc = ((doc.get("backends") or {}).get("vulkan") or {})
    vulkan_strategy = vulkan_doc.get("strategy") or {}
    vulkan_sets: List[VulkanDescriptorSet] = []
    for descriptor_set in vulkan_strategy.get("descriptor_sets") or []:
        vulkan_sets.append(
            VulkanDescriptorSet(
                id=str(descriptor_set.get("id")),
                set=int(descriptor_set.get("set")),
            )
        )

    vulkan_bindings: List[VulkanBinding] = []
    for binding in vulkan_strategy.get("bindings") or []:
        vulkan_bindings.append(
            VulkanBinding(
                id=str(binding.get("id")),
                set=str(binding.get("set")),
                binding=int(binding.get("binding")),
                descriptor_type=str(binding.get("descriptor_type")),
                descriptor_count=_coerce_int(binding.get("descriptor_count")),
                variable_count=bool(binding.get("variable_count", False)),
                update_after_bind=bool(binding.get("update_after_bind", False)),
            )
        )

    vulkan_realizations: List[VulkanDomainRealization] = []
    for realization in vulkan_strategy.get("domain_realizations") or []:
        vulkan_realizations.append(
            VulkanDomainRealization(
                domain=str(realization.get("domain")),
                binding=str(realization.get("binding")),
                array_element_base=int(realization.get("array_element_base")),
            )
        )

    vulkan_pipeline_layout: List[VulkanPipelineEntry] = []
    for entry in vulkan_doc.get("pipeline_layout") or []:
        vulkan_pipeline_layout.append(
            VulkanPipelineEntry(
                type=str(entry.get("type")),
                id=str(entry.get("id")),
                set_ref=entry.get("set_ref"),
                size_bytes=_coerce_int(entry.get("size_bytes")),
                stages=list(entry.get("stages") or []),
            )
        )

    return Model(
        meta=meta,
        defaults=defaults,
        index_spaces=index_spaces,
        abi_domains=abi_domains,
        d3d12=D3D12Backend(
            heaps=d3d12_heaps,
            tables=d3d12_tables,
            domain_realizations=d3d12_realizations,
            root_signature=d3d12_root_signature,
        ),
        vulkan=VulkanBackend(
            descriptor_sets=vulkan_sets,
            bindings=vulkan_bindings,
            domain_realizations=vulkan_realizations,
            pipeline_layout=vulkan_pipeline_layout,
        ),
    )
