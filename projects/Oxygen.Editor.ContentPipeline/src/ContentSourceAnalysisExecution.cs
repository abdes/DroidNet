// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>One destination-free native dependency frontier.</summary>
/// <param name="OperationId">The coordinator's operation identity.</param>
/// <param name="InputRoot">The logical authoring root for relative sources.</param>
/// <param name="OperationRoot">The operation-owned directory for manifests and reports.</param>
/// <param name="Jobs">Native recipes, including their per-job output layouts.</param>
public sealed record ContentSourceAnalysisExecution(
    Guid OperationId,
    string InputRoot,
    string OperationRoot,
    IReadOnlyList<ContentImportJob> Jobs)
{
    /// <summary>Gets artifacts borrowed from the cook owner through worker drain.</summary>
    public NativeArtifactLease? Artifacts { get; init; }

    /// <summary>Gets captured replacement bytes addressed by their retained logical paths.</summary>
    public NativeCapturedInputSet? CapturedInputs { get; init; }
}
