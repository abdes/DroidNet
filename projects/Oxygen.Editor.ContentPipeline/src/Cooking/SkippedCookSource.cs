// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Cooking;

/// <summary>A changed source that cook on change leaves uncooked until a change fixes its errors, or those of the sources it uses.</summary>
public sealed record SkippedCookSource
{
    /// <summary>Gets the changed source.</summary>
    public required AssetPath Source { get; init; }

    /// <summary>Gets how many errors the facts report: its own, or those of <see cref="Dependencies"/>.</summary>
    public required int ErrorCount { get; init; }

    /// <summary>Gets the sources it uses whose facts report the errors, such as a scene's scripts; empty when the errors are its own.</summary>
    public IReadOnlyList<AssetPath> Dependencies { get; init; } = [];
}
