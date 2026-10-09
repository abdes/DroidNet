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

    /// <summary>Gets the logical authoring root used by native recipes.</summary>
    public string InputRoot => this.Project.ProjectRoot;

    /// <summary>Gets the operation-owned directory for source projections before capture.</summary>
    internal string? PreparationRoot { get; init; }

    /// <summary>Gets the native catalog already queried under this operation's artifact lease.</summary>
    internal BuiltinGeometryCatalog? BuiltinCatalog { get; init; }

    /// <summary>Gets builtin descriptors already materialized by this operation.</summary>
    internal IReadOnlyDictionary<Uri, ContentCookInput> PreparedBuiltins { get; init; }
        = System.Collections.Immutable.ImmutableDictionary<Uri, ContentCookInput>.Empty;

    /// <summary>Gets the native recipes already used for source analysis.</summary>
    internal IReadOnlyDictionary<Uri, ContentImportJob> NativeJobs { get; init; }
        = System.Collections.Immutable.ImmutableDictionary<Uri, ContentImportJob>.Empty;

    /// <summary>Gets the exact scene projections accepted during discovery.</summary>
    internal IReadOnlyDictionary<Uri, SceneDescriptorGenerationResult> SceneDescriptors { get; init; }
        = System.Collections.Immutable.ImmutableDictionary<Uri, SceneDescriptorGenerationResult>.Empty;

    /// <summary>Gets the admitted authored and generated inputs for native cooking.</summary>
    internal Import.NativeCapturedInputSet? CapturedInputs { get; init; }

    /// <summary>Gets the private native output root when the cook is preparing a publication.</summary>
    internal Publication.CookStagingRoot? Output { get; init; }

    /// <summary>Gets exact imported outputs that must exist before this request can succeed.</summary>
    internal System.Collections.Immutable.ImmutableArray<Uri> RequiredImportedOutputs { get; init; } = [];

    /// <summary>Gets a value indicating whether explicit user intent permits changed retained model inputs.</summary>
    internal bool AllowImportedSourceChanges { get; init; } = true;

    /// <summary>
    /// Gets a value indicating whether a save or preview queued this cook. Its source may have been moved or deleted
    /// since then; that ends the request instead of failing it.
    /// </summary>
    internal bool IsAutomatic { get; init; }

    /// <summary>Gets ordered native lookup roots, including private roots for affected project mounts.</summary>
    internal IReadOnlyList<string> CookedContextRoots { get; init; } = [];

    /// <summary>Gets sources whose validated products should be omitted from native manifests.</summary>
    internal System.Collections.Immutable.ImmutableHashSet<Uri> ReusableSources { get; init; } = [];

    /// <summary>Gets discovered source dependencies used to order native jobs within this scope.</summary>
    internal System.Collections.Immutable.ImmutableDictionary<Uri, System.Collections.Immutable.ImmutableArray<Uri>> InputDependencies { get; init; }
        = System.Collections.Immutable.ImmutableDictionary<Uri, System.Collections.Immutable.ImmutableArray<Uri>>.Empty;

    /// <summary>Gets prior source ownership used to reject imported-output collisions.</summary>
    internal Incremental.CookProvenance? PreviousProvenance { get; init; }

    /// <summary>Gets prior native inventory metadata for imported-output identity checks.</summary>
    internal System.Collections.Immutable.ImmutableDictionary<string, Inspection.CookedInventoryReport> PreviousInventories { get; init; }
        = System.Collections.Immutable.ImmutableDictionary<string, Inspection.CookedInventoryReport>.Empty;

    /// <summary>Gets the explicit source replacement captured instead of the currently retained bytes.</summary>
    internal Import.SceneImportRequest? ImportReplacement { get; init; }

    /// <summary>Gets the owning run's callback for retaining an incoming replacement candidate for Retry.</summary>
    internal Action<Import.SceneImportRequest>? RetainReplacementCandidate { get; init; }
}
