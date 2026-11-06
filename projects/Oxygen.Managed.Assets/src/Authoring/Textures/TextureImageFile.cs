// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Authoring.Textures;

/// <summary>An image a texture reads, and the file that holds it.</summary>
/// <param name="Reference">The file reference that names the image.</param>
/// <param name="File">The file, which need not exist; <see langword="null"/> when the reference names no file, as for a
/// mount this machine has not bound.</param>
public sealed record TextureImageFile(string Reference, FileSystemPath? File);
