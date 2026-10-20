// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Cooking;

/// <summary>A changed source that cook on change leaves uncooked until a change fixes its errors.</summary>
public sealed record SkippedCookSource
{
    /// <summary>Gets the changed source.</summary>
    public required AssetPath Source { get; init; }

    /// <summary>Gets how many errors its facts report.</summary>
    public required int ErrorCount { get; init; }
}
