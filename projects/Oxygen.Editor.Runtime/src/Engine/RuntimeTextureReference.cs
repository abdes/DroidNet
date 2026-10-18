// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Identifies an authored texture in an explicitly mounted cooked source.</summary>
/// <param name="Asset">The retained authoring identity used in diagnostics.</param>
/// <param name="CookedRoot">The absolute cooked source root.</param>
/// <param name="DescriptorRelativePath">The descriptor path within that source.</param>
public sealed record RuntimeTextureReference(AssetPath Asset, string CookedRoot, string DescriptorRelativePath)
{
    /// <summary>Gets the logical project mount to follow across generation replacement; null retains an explicit external locator.</summary>
    public string? ProjectMount { get; init; }
}
