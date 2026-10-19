// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Authoring.Textures;

/// <summary>
/// A texture's authored source, read from <c>*.otex.json</c>: the images the cooker reads and how it cooks them.
/// </summary>
/// <remarks>
/// Image paths are as the descriptor spells them: relative to its folder, or rooted. Fields this model does not name,
/// such as mips or HDR handling, are kept when the source is written back.
/// </remarks>
public sealed record TextureSource
{
    /// <summary>Initializes a new instance of the <see cref="TextureSource"/> class.</summary>
    /// <param name="source">The primary image.</param>
    public TextureSource(string source)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(source);
        this.Source = source;
    }

    /// <summary>Gets the texture's display name.</summary>
    public string? Name { get; init; }

    /// <summary>Gets the cooked texture's virtual path, or <see langword="null"/> for a texture named by its content
    /// hash.</summary>
    public AssetPath? VirtualPath { get; init; }

    /// <summary>Gets the primary image. A six-face cube names its +X face; the cooker finds the others by name.</summary>
    public string Source { get; init; }

    /// <summary>Gets the images the texture adds for array layers, mips or volume slices.</summary>
    public IReadOnlyList<TextureSourceMapping> Sources { get; init; } = [];

    /// <summary>Gets what the texture is for, such as <c>albedo</c> or <c>normal</c>, which picks the cooker's
    /// defaults.</summary>
    public string? Intent { get; init; }

    /// <summary>Gets the color space the cooker decodes the images in: <c>srgb</c> or <c>linear</c>.</summary>
    public string? ColorSpace { get; init; }

    /// <summary>Gets the cooked pixel format, such as <c>bc7_srgb</c> or <c>rgba16f</c>.</summary>
    public string? Format { get; init; }

    /// <summary>Gets how the cooker makes a cube texture, or <see langword="null"/> for a texture that is not a
    /// cube.</summary>
    public TextureCubeSettings? Cube { get; init; }

    /// <summary>Gets every image the texture reads: <see cref="Source"/>, then each of <see cref="Sources"/>.</summary>
    public IReadOnlyList<string> Images => [this.Source, .. this.Sources.Select(static mapping => mapping.File)];

    /// <summary>Gets whether the texture cooks to a cube.</summary>
    public bool IsCube => this.Cube?.MakesCube == true;

    /// <summary>Gets whether the cooked format keeps high dynamic range values.</summary>
    public bool StoresRadiance => this.Format is "rgba16f" or "rgba32f";

    /// <summary>Gets the descriptor as read, which keeps the fields this model does not name.</summary>
    internal JsonElement? Descriptor { get; init; }

    /// <summary>Resolves every image the texture reads to a file, as the cooker does.</summary>
    /// <param name="descriptor">The texture descriptor file.</param>
    /// <returns>The images, in <see cref="Images"/> order.</returns>
    public IReadOnlyList<FileSystemPath> ResolveImages(FileSystemPath descriptor)
    {
        ArgumentNullException.ThrowIfNull(descriptor);
        var folder = descriptor.Parent ?? throw new ArgumentException("The descriptor has no folder.", nameof(descriptor));
        return [.. this.Images.Select(image => Path.IsPathRooted(image) ? FileSystemPath.From(image) : folder.Combine(image))];
    }
}
