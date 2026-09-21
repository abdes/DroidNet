// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Identifies an authored texture in an explicitly mounted cooked source.</summary>
/// <param name="AssetUri">The retained authoring identity used in diagnostics.</param>
/// <param name="CookedRoot">The absolute cooked source root.</param>
/// <param name="DescriptorRelativePath">The descriptor path within that source.</param>
public sealed record RuntimeTextureReference(Uri AssetUri, string CookedRoot, string DescriptorRelativePath);
