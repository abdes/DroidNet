// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Import;

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>Identifies an immutable private saved-input set retained through cook and publication.</summary>
/// <param name="Operation">The owning project operation.</param>
/// <param name="InputRoot">The private directory read by native jobs.</param>
/// <param name="BuildFingerprint">The compatible artifact/schema identity.</param>
/// <param name="InputIdentity">The content identity independent of local paths and operation IDs.</param>
/// <param name="Inputs">The original identities, paths, and captured hashes.</param>
/// <param name="Documents">The saved revisions of participating open documents.</param>
public sealed record CookInputSnapshot(
    ContentCookOperation Operation,
    string InputRoot,
    string BuildFingerprint,
    string InputIdentity,
    ImmutableArray<CookSnapshotInput> Inputs,
    ImmutableArray<CookDocumentState> Documents)
{
    /// <summary>Gets foreign cooked inputs held alongside the private authoring snapshot.</summary>
    public ImmutableArray<CookedDependencySnapshot> CookedDependencies { get; init; } = [];

    /// <summary>Gets the reviewed retained-source baseline installed with these cooked outputs.</summary>
    internal Publication.CookSourceReplacement? SourceReplacement { get; init; }

    /// <summary>Creates the native admission map under retained project identities.</summary>
    /// <returns>Captured files and probes; no source paths are reopened.</returns>
    public NativeCapturedInputSet CreateNativeInputs()
        => new([.. this.Inputs.Select(input =>
        {
            var logicalPath = Path.GetFullPath(Path.Combine(this.Operation.Project.ProjectRoot, input.RelativePath));
            NativeCapturedFile? file = input.Kind == CookSnapshotInputKind.File
                ? new(
                    Path.Combine(this.InputRoot, input.RelativePath),
                    input.Metadata?.Size ?? throw new InvalidOperationException("A captured file is missing its original metadata."),
                    input.DiscoveryHash.ToLowerInvariant())
                : null;
            return new NativeCapturedInput(logicalPath, !input.IsAbsent, input.Metadata, file);
        })]);
}
