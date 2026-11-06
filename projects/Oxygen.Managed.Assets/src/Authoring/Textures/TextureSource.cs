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
/// Images are file references, as the descriptor spells them: relative to the texture's identity, or in another mount.
/// Fields this model does not name, such as mips or HDR handling, are kept when the source is written back.
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

    /// <summary>
    /// Gets the cooker's names of the six faces of a cube, each set in +X, -X, +Y, -Y, +Z, -Z order. A face is named
    /// by the name the faces share and the suffix of its face, such as <c>sky_px.hdr</c>.
    /// </summary>
    public static IReadOnlyList<IReadOnlyList<string>> CubeFaceSuffixSets { get; } =
    [
        ["_px", "_nx", "_py", "_ny", "_pz", "_nz"],
        ["_posx", "_negx", "_posy", "_negy", "_posz", "_negz"],
        ["_right", "_left", "_top", "_bottom", "_front", "_back"],
    ];

    /// <summary>Gets the texture's display name.</summary>
    public string? Name { get; init; }

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

    /// <summary>Gets the images the descriptor names: <see cref="Source"/>, then each of <see cref="Sources"/>.</summary>
    /// <remarks>A six-face cube names only one face; <see cref="ResolveImages"/> finds the other five.</remarks>
    public IReadOnlyList<string> Images => [this.Source, .. this.Sources.Select(static mapping => mapping.File)];

    /// <summary>
    /// Gets whether the texture is a cube made of six face files, which the cooker finds by name from
    /// <see cref="Source"/>: a cube with no other source, no panorama projection and no layout.
    /// </summary>
    public bool IsSixFaceCube => this.Sources.Count == 0 && this.Cube is { Cubemap: true, EquirectToCube: not true, Layout: null };

    /// <summary>Gets whether the texture cooks to a cube.</summary>
    public bool IsCube => this.Cube?.MakesCube == true;

    /// <summary>Gets whether the cooked format keeps high dynamic range values.</summary>
    public bool StoresRadiance => this.Format is "rgba16f" or "rgba32f";

    /// <summary>Gets the descriptor as read, which keeps the fields this model does not name.</summary>
    internal JsonElement? Descriptor { get; init; }

    /// <summary>Resolves every image the texture reads to a file, as the cooker does.</summary>
    /// <param name="identity">The texture's identity, which its references are relative to.</param>
    /// <param name="locator">Turns a reference into a file.</param>
    /// <param name="exists">Whether a file exists; the six faces of a cube are the first set of names whose six files
    /// all exist, as the cooker finds them.</param>
    /// <returns>The images, in <see cref="Images"/> order; for a six-face cube whose reference names a file, its faces in
    /// +X, -X, +Y, -Y, +Z, -Z order, named by the first set when no set is complete.</returns>
    public IReadOnlyList<TextureImageFile> ResolveImages(AssetPath identity, IFileReferenceLocator locator, Func<FileSystemPath, bool> exists)
    {
        ArgumentNullException.ThrowIfNull(identity);
        ArgumentNullException.ThrowIfNull(locator);
        ArgumentNullException.ThrowIfNull(exists);
        IReadOnlyList<TextureImageFile> images = [.. this.Images.Select(image => new TextureImageFile(image, locator.LocateReference(identity, image)))];
        return this.IsSixFaceCube && images[0] is { File: { } file } ? FindFaces(this.Source, file, exists) : images;
    }

    private static IReadOnlyList<TextureImageFile> FindFaces(string reference, FileSystemPath source, Func<FileSystemPath, bool> exists)
    {
        var folder = source.Parent!;
        var stem = Path.GetFileNameWithoutExtension(source.Name);
        var extension = Path.GetExtension(source.Name);
        IReadOnlyList<TextureImageFile>? first = null;
        foreach (var suffixes in CubeFaceSuffixSets)
        {
            var shared = suffixes.FirstOrDefault(suffix => stem.EndsWith(suffix, StringComparison.OrdinalIgnoreCase)) is { } suffix
                ? stem[..^suffix.Length]
                : stem;
            IReadOnlyList<TextureImageFile> faces = [.. suffixes.Select(face => new TextureImageFile(reference, folder.Combine(shared + face + extension)))];
            if (faces.All(face => exists(face.File!)))
            {
                return faces;
            }

            first ??= faces;
        }

        return first!;
    }
}
