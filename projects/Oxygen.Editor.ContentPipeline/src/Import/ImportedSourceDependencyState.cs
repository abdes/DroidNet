// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Native-discovered dependency paths for one exact primary-file revision.</summary>
/// <param name="PrimaryHash">The inspected primary bytes.</param>
/// <param name="Files">The project-relative source files belonging to that revision.</param>
public sealed record ImportedSourceDependencyState(string PrimaryHash, ImmutableArray<string> Files)
{
    /// <summary>Gets the saved source, dependency and settings identity approved by the published import.</summary>
    public string? ContentFingerprint { get; init; }
}
