// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Cooked loose-root inspection result.
/// </summary>
/// <param name="CookedRoot">The inspected cooked root.</param>
/// <param name="Succeeded">Whether inspection succeeded.</param>
/// <param name="SourceIdentity">The loose cooked source identity, when available.</param>
/// <param name="Assets">Inspected asset entries.</param>
/// <param name="Files">Inspected file entries.</param>
/// <param name="Diagnostics">Inspection diagnostics.</param>
public sealed record CookInspectionResult(
    string CookedRoot,
    bool Succeeded,
    Guid? SourceIdentity,
    IReadOnlyList<CookedAssetEntry> Assets,
    IReadOnlyList<CookedFileEntry> Files,
    IReadOnlyList<DiagnosticRecord> Diagnostics);
