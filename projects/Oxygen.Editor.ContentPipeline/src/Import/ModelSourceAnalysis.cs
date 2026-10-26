// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.Models;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>What the analysis of a model source found before it is imported.</summary>
/// <param name="Complete">Whether the source was analyzed; an incomplete analysis has no facts.</param>
/// <param name="Facts">The source's format, units, axes, counts, scenes and names; null when the analysis did not complete.</param>
/// <param name="Diagnostics">The diagnostics the import itself will report, such as a dropped feature or a missing texture.</param>
public sealed record ModelSourceAnalysis(bool Complete, ModelSourceFacts? Facts, ImmutableArray<DiagnosticRecord> Diagnostics);
