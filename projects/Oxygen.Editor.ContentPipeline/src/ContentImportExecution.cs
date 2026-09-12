// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Explicit physical input and operation paths for one native manifest execution.</summary>
/// <param name="OperationId">The coordinator's operation identity.</param>
/// <param name="InputRoot">The absolute root used to resolve manifest source paths.</param>
/// <param name="OperationRoot">The absolute operation-owned directory retaining temporary manifests.</param>
/// <param name="Manifest">The native manifest, including its independent physical output root.</param>
public sealed record ContentImportExecution(
    Guid OperationId,
    string InputRoot,
    string OperationRoot,
    ContentImportManifest Manifest)
{
    /// <summary>Gets artifacts borrowed from the cook owner, which retains them through worker drain.</summary>
    public NativeArtifactLease? Artifacts { get; init; }
}
