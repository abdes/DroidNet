// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>A finite read-only report of the requested published scope.</summary>
/// <param name="ProjectId">The originating project.</param>
/// <param name="ScopeUri">The requested asset/folder scope, or null for all project output.</param>
/// <param name="CapturedAt">The time this inspection completed.</param>
/// <param name="Roots">Inspected physical roots with scoped assets and root-wide files/validation.</param>
public sealed record CookedOutputReport(Guid ProjectId, Uri? ScopeUri, DateTimeOffset CapturedAt, IReadOnlyList<CookedRootReport> Roots);
