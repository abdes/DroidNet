// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.IO.Abstractions;
using Oxygen.Managed.Assets.Authoring.Textures;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Textures;

/// <summary>
/// A texture: its current <see cref="TextureSource"/>, replaced whole on each change, and the rules of textures that
/// hold whatever shows it.
/// </summary>
/// <remarks>
/// A texture has two authored files: the image, which is never rewritten, and the descriptor, which names the image
/// relative to its own folder and carries the cook settings.
/// </remarks>
public sealed class Texture
{
    /// <summary>Initializes a new instance of the <see cref="Texture"/> class.</summary>
    /// <param name="source">The texture's source.</param>
    public Texture(TextureSource source)
    {
        ArgumentNullException.ThrowIfNull(source);
        this.Source = source;
    }

    /// <summary>Occurs on the thread that replaced <see cref="Source"/>, after it changed.</summary>
    public event EventHandler? Changed;

    /// <summary>Gets the texture's current source.</summary>
    public TextureSource Source { get; private set; }

    /// <summary>Gets a value indicating whether the texture cooks to a cube.</summary>
    public bool IsCube => this.Source.IsCube;

    /// <summary>
    /// Gets a value indicating whether the cooked texture keeps float (HDR) texels. Only such a cube can light a scene;
    /// an LDR cube is display-only.
    /// </summary>
    public bool StoresRadiance => this.Source.StoresRadiance;

    /// <summary>Reads a texture from its descriptor file.</summary>
    /// <param name="fileSystem">The file system that holds the descriptor.</param>
    /// <param name="descriptorPath">The texture descriptor file.</param>
    /// <returns>The texture, or <see langword="null"/> when the descriptor cannot be read or is not a valid texture
    /// descriptor.</returns>
    public static Texture? TryRead(IFileSystem fileSystem, FileSystemPath descriptorPath)
    {
        ArgumentNullException.ThrowIfNull(fileSystem);
        ArgumentNullException.ThrowIfNull(descriptorPath);
        try
        {
            return new Texture(TextureSourceReader.Read(fileSystem.File.ReadAllBytes(descriptorPath.FullPath)));
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or AssetFormatException)
        {
            return null;
        }
    }

    /// <summary>Gets a value indicating whether every image the texture reads exists.</summary>
    /// <param name="fileSystem">The file system that holds the images.</param>
    /// <param name="descriptorPath">The texture's descriptor file, which its image paths are relative to.</param>
    /// <returns><see langword="true"/> when each image exists.</returns>
    public bool HasSourceImage(IFileSystem fileSystem, FileSystemPath descriptorPath)
    {
        ArgumentNullException.ThrowIfNull(fileSystem);
        return this.Source.ResolveImages(descriptorPath, Exists).All(Exists);

        bool Exists(FileSystemPath image) => fileSystem.File.Exists(image.FullPath);
    }

    /// <summary>Replaces the source, and raises <see cref="Changed"/> when it is a different one.</summary>
    /// <param name="source">The new source.</param>
    public void Replace(TextureSource source)
    {
        ArgumentNullException.ThrowIfNull(source);
        if (ReferenceEquals(this.Source, source))
        {
            return;
        }

        this.Source = source;
        this.Changed?.Invoke(this, EventArgs.Empty);
    }
}
