// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>A native-generated descriptor and its stable output address.</summary>
/// <param name="Name">The native descriptor identifier.</param>
/// <param name="VirtualPath">The cooked output path.</param>
/// <param name="Descriptor">The complete engine-owned JSON payload.</param>
public sealed record BuiltinDescriptorContribution(string Name, string VirtualPath, JsonElement Descriptor);
