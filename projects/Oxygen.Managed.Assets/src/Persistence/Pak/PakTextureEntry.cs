// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets.Persistence.Pak;

/// <summary>One texture a pak's browse index names, joined with its texture-table record.</summary>
/// <param name="VirtualPath">The canonical virtual path of the texture's descriptor.</param>
/// <param name="TextureIndex">The texture's entry in the pak's texture table.</param>
/// <param name="Texture">The texture-table record.</param>
public sealed record PakTextureEntry(string VirtualPath, uint TextureIndex, TextureResourceRecord Texture);
