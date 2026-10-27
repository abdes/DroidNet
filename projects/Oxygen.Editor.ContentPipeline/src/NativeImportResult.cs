// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
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
    /// <summary>Gets the files each native job wrote, shared tables included, keyed by captured source-relative path.</summary>
    public ImmutableDictionary<string, ImmutableArray<string>> OutputsBySource { get; init; }
        = [];

    /// <summary>Gets the virtual paths of the assets and textures each source produced, keyed by captured source-relative path.</summary>
    public ImmutableDictionary<string, ImmutableHashSet<string>> ProductsBySource { get; init; }
        = [];

    /// <summary>Gets what each model import established, the report's <c>model_import</c>, keyed by the model's captured
    /// source-relative path; the publication commits it to the model's import record.</summary>
    public ImmutableDictionary<string, JsonElement> ModelImports { get; init; }
        = [];
}
