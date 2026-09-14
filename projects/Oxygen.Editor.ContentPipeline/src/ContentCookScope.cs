// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Resolved scope for an explicit content cook operation.
/// </summary>
/// <param name="Project">The active project context.</param>
/// <param name="CookScope">The project cook scope.</param>
/// <param name="Inputs">The authored inputs included in the cook.</param>
/// <param name="TargetKind">The user-visible cook target kind.</param>
public sealed record ContentCookScope(
    ProjectContext Project,
    ProjectCookScope CookScope,
    IReadOnlyList<ContentCookInput> Inputs,
    CookTargetKind TargetKind)
{
    /// <summary>Gets the requested asset or folder identity before mapping imported output to its source.</summary>
    public Uri? ScopeUri { get; init; }

    /// <summary>Gets the coherent saved input set when preparation belongs to a cook operation.</summary>
    public CookInputSnapshot? Snapshot { get; init; }

    /// <summary>Gets the compatible artifacts borrowed from the owning cook operation.</summary>
    public NativeArtifactLease? Artifacts { get; init; }

    /// <summary>Gets the physical input root used by descriptor generation and native jobs.</summary>
    public string InputRoot => this.Snapshot?.InputRoot ?? this.Project.ProjectRoot;

    /// <summary>Gets the private native output root when the cook is preparing a publication.</summary>
    public string? StagingOutputRoot { get; init; }

    /// <summary>Gets exact imported outputs that must exist before this request can succeed.</summary>
    internal System.Collections.Immutable.ImmutableArray<Uri> RequiredImportedOutputs { get; init; } = [];

    /// <summary>Gets a value indicating whether explicit user intent permits changed retained model inputs.</summary>
    internal bool AllowImportedSourceChanges { get; init; } = true;

    /// <summary>Gets ordered native lookup roots, including private roots for affected project mounts.</summary>
    internal IReadOnlyList<string> CookedContextRoots { get; init; } = [];

    /// <summary>Gets sources whose validated products should be omitted from native manifests.</summary>
    internal System.Collections.Immutable.ImmutableHashSet<Uri> ReusableSources { get; init; } = [];

    /// <summary>Gets prior source ownership used to reject imported-output collisions.</summary>
    internal Incremental.CookProvenance? PreviousProvenance { get; init; }

    /// <summary>Gets the explicit source replacement captured instead of the currently retained bytes.</summary>
    internal Import.SceneImportRequest? ImportReplacement { get; init; }

    /// <summary>Gets the owning run's callback for retaining an incoming replacement candidate for Retry.</summary>
    internal Action<Import.SceneImportRequest>? RetainReplacementCandidate { get; init; }
}
