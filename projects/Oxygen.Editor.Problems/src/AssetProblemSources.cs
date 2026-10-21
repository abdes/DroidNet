// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Diagnostics;

namespace Oxygen.Editor.Problems;

/// <summary>A fixed list of problem sources.</summary>
/// <param name="sources">The sources, in the order they are listed; a source listed twice is kept once.</param>
public sealed class AssetProblemSources(IEnumerable<IAssetProblemSource> sources) : IAssetProblemSources
{
    /// <inheritdoc />
    public IReadOnlyList<IAssetProblemSource> Sources { get; } = [.. sources.Distinct()];
}
