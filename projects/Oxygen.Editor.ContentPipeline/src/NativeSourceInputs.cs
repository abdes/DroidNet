// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>The inputs a native source analysis names, classified against the folder they are kept under.</summary>
/// <param name="Files">The inputs under the root, with the observations capture must verify.</param>
/// <param name="Omitted">Missing files outside the root; the import proceeds without them.</param>
/// <param name="Outside">Existing files outside the root, which cannot be kept.</param>
internal sealed record NativeSourceInputs(
    ImmutableArray<CookSnapshotInput> Files,
    ImmutableArray<FileSystemPath> Omitted,
    ImmutableArray<FileSystemPath> Outside)
{
    /// <summary>A warning for each omitted file.</summary>
    /// <param name="operationId">The operation that analyzed the source.</param>
    /// <param name="domain">The reporting domain.</param>
    /// <param name="virtualPath">The source's virtual path, when it has one.</param>
    /// <returns>The warnings.</returns>
    public ImmutableArray<DiagnosticRecord> OmittedDiagnostics(Guid operationId, FailureDomain domain, string? virtualPath)
        => [.. this.Omitted.Select(path => new DiagnosticRecord
        {
            OperationId = operationId,
            Domain = domain,
            Severity = DiagnosticSeverity.Warning,
            Code = AssetImportDiagnosticCodes.MissingDependencyOmitted,
            Message = $"The source names '{path.FullPath}', a missing file outside its folder; the import proceeds without it.",
            AffectedPath = path.FullPath,
            AffectedVirtualPath = virtualPath,
        }),];

    /// <summary>An error for each existing file outside the root.</summary>
    /// <param name="operationId">The operation that analyzed the source.</param>
    /// <param name="domain">The reporting domain.</param>
    /// <param name="virtualPath">The source's virtual path, when it has one.</param>
    /// <returns>The errors.</returns>
    public ImmutableArray<DiagnosticRecord> OutsideDiagnostics(Guid operationId, FailureDomain domain, string? virtualPath)
        => [.. this.Outside.Select(path => new DiagnosticRecord
        {
            OperationId = operationId,
            Domain = domain,
            Severity = DiagnosticSeverity.Error,
            Code = AssetImportDiagnosticCodes.DependencyOutsideSource,
            Message = $"The source uses '{path.FullPath}', which lies outside the folder its inputs are kept under. Move it beside the source.",
            AffectedPath = path.FullPath,
            AffectedVirtualPath = virtualPath,
        }),];
}
