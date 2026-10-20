// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Cooking;

/// <summary>The changed sources one cook on change covers, and those it leaves out for their problems.</summary>
public sealed record CookChanges
{
    /// <summary>Gets the changed sources to cook.</summary>
    public required IReadOnlyList<AssetPath> Sources { get; init; }

    /// <summary>Gets the changed sources left uncooked because their facts report errors.</summary>
    public IReadOnlyList<SkippedCookSource> Skipped { get; init; } = [];
}
