// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>A physical root inspected under one lease, with separately verified source associations.</summary>
/// <param name="Name">The project or local mount name.</param>
/// <param name="IsPresent">Whether output files exist in the root, including incomplete output.</param>
/// <param name="Inspection">Native index assets and protected filesystem facts; assets are restricted to the requested scope.</param>
/// <param name="Validation">Optional integrity validation of this same root generation.</param>
/// <param name="Provenance">Source/dependency facts whose output hashes match the inspected generation.</param>
public sealed record CookedRootReport(string Name, bool IsPresent, CookInspectionResult Inspection, CookValidationResult? Validation, IReadOnlyList<CookedAssetProvenance> Provenance);
