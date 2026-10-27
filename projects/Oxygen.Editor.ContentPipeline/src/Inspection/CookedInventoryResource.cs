// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>A native texture sidecar and its observed table slot; the slot is not a durable identity.</summary>
/// <param name="Kind">The native resource kind.</param>
/// <param name="VirtualPath">The logical name the texture is addressed by.</param>
/// <param name="DescriptorPath">The indexed physical member.</param>
/// <param name="ResourceIndex">The validated table slot, or null when integrity failed.</param>
public sealed record CookedInventoryResource(string Kind, string VirtualPath, string DescriptorPath, uint? ResourceIndex);
