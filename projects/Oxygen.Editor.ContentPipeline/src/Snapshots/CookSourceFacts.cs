// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Import;

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>Native dependency facts attributed to their authored owner.</summary>
internal sealed record CookSourceFacts(
    ContentCookInput Input,
    ImmutableArray<NativeLogicalDependency> Outputs,
    ImmutableArray<NativeLogicalDependency> References,
    ImmutableArray<CookSnapshotInput> Files)
{
    /// <summary>Gets the native job prepared for this cook, when available.</summary>
    public ContentImportJob? Job { get; init; }

    /// <summary>Gets the projected scene descriptor, when prepared for this cook.</summary>
    public SceneDescriptorGenerationResult? Scene { get; init; }

    /// <summary>Gets a value indicating whether accepted facts cannot describe the saved source.</summary>
    public bool RequiresAnalysis { get; init; }
}
