// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Result returned by the engine content-pipeline adapter.
/// </summary>
/// <param name="Succeeded">Whether native import succeeded.</param>
/// <param name="Diagnostics">Adapted native diagnostics.</param>
public sealed record NativeImportResult(bool Succeeded, IReadOnlyList<DiagnosticRecord> Diagnostics)
{
    /// <summary>Gets the exact files produced by source-model jobs, when native reporting was requested.</summary>
    public IReadOnlyList<string>? OutputFiles { get; init; }

    /// <summary>Gets native allocation records keyed by their captured source-relative paths.</summary>
    public ImmutableDictionary<string, NativeMaterialSlotProvenance> MaterialSlotProvenance { get; init; }
        = ImmutableDictionary<string, NativeMaterialSlotProvenance>.Empty;
}
