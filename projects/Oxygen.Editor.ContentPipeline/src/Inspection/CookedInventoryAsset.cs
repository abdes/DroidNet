// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>A native asset's identity and physical descriptor.</summary>
/// <param name="Key">The native asset key.</param>
/// <param name="Type">The native asset type.</param>
/// <param name="VirtualPath">The logical name.</param>
/// <param name="DescriptorPath">The indexed physical member.</param>
public sealed record CookedInventoryAsset(Guid Key, byte Type, string VirtualPath, string DescriptorPath);
