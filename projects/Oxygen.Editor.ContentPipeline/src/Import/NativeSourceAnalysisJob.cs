// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>One native recipe's declared outputs and observed inputs.</summary>
/// <param name="Id">The submitted job identifier.</param>
/// <param name="JobType">The native import family.</param>
/// <param name="SourcePath">The logical primary source.</param>
/// <param name="Complete">Whether this source was analyzed successfully.</param>
/// <param name="Outputs">Declared native outputs; cooking still validates payloads.</param>
/// <param name="References">Logical asset references for project/library resolution.</param>
/// <param name="Files">Files declared by native preparation.</param>
/// <param name="Observations">Verified facts that capture must reproduce.</param>
/// <param name="Diagnostics">Native issues attributed to this source.</param>
public sealed record NativeSourceAnalysisJob(
    string Id,
    string JobType,
    string SourcePath,
    bool Complete,
    ImmutableArray<NativeLogicalDependency> Outputs,
    ImmutableArray<NativeLogicalDependency> References,
    ImmutableArray<NativeSourceFileDependency> Files,
    ImmutableArray<NativeSourceObservation> Observations,
    ImmutableArray<DiagnosticRecord> Diagnostics);
