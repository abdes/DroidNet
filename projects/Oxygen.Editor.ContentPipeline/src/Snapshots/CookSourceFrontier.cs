// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>Native facts and operation-owned generated inputs for a dependency frontier.</summary>
internal sealed record CookSourceFrontier(
    ImmutableArray<CookSourceFacts> Sources,
    ImmutableArray<NativeCapturedInput> GeneratedInputs,
    ImmutableArray<DiagnosticRecord> Diagnostics);
