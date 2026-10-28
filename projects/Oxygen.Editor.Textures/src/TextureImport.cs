// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.IO.Abstractions;
using System.Text.Json;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Authoring.Textures;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Textures;

/// <summary>
/// Imports an image as a project texture: copies the image into an authoring folder and writes its descriptor beside it.
/// </summary>
/// <param name="fileSystem">The file system that holds the images and the project.</param>
public sealed class TextureImport(IFileSystem fileSystem)
{
    private static readonly HashSet<string> ImageExtensions = new(AssetFileTypes.Suffixes(AssetFileCategory.Image), StringComparer.OrdinalIgnoreCase);

    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };

    private readonly IFileSystem fileSystem = fileSystem ?? throw new ArgumentNullException(nameof(fileSystem));

    /// <summary>Gets what an imported texture can be for, which picks the cooker's defaults.</summary>
    public static IReadOnlyList<string> Intents { get; } = ["albedo", "normal", "roughness", "metallic", "ao", "orm", "emissive", "opacity", "data", "height", "hdr_env", "hdr_probe"];

    /// <summary>Gets the color spaces the cooker can decode an imported image in.</summary>
    public static IReadOnlyList<string> ColorSpaces { get; } = ["srgb", "linear"];

    /// <summary>Gets the pixel formats an imported texture can cook to.</summary>
    public static IReadOnlyList<string> Formats { get; } = ["rgba8", "rgba8_srgb", "bc7", "bc7_srgb", "rgba16f", "rgba32f"];

    /// <summary>Gets the supported standalone image extensions.</summary>
    public static IReadOnlyCollection<string> SupportedExtensions => ImageExtensions;

    /// <summary>Gets the suffixes an imported six-face cube gives its face files, in +X, -X, +Y, -Y, +Z, -Z order.</summary>
    public static IReadOnlyList<string> FaceSuffixes => TextureSource.CubeFaceSuffixSets[0];

    /// <summary>Returns whether a path extension is supported by the standalone image import action.</summary>
    /// <param name="path">The selected image path.</param>
    /// <returns>True for supported source image formats.</returns>
    public static bool IsSupportedImage(string path)
        => !string.IsNullOrWhiteSpace(path) && ImageExtensions.Contains(Path.GetExtension(path));

    /// <summary>Returns whether an image holds linear high dynamic range values: a Radiance HDR or OpenEXR image.</summary>
    /// <param name="path">The selected image path.</param>
    /// <returns>True for <c>.hdr</c> and <c>.exr</c> images.</returns>
    public static bool IsHdrImage(string path)
    {
        var extension = Path.GetExtension(path);
        return string.Equals(extension, ".hdr", StringComparison.OrdinalIgnoreCase)
            || string.Equals(extension, ".exr", StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>Infers how a single image lays out a cube from its aspect ratio.</summary>
    /// <param name="width">The image width in pixels.</param>
    /// <param name="height">The image height in pixels.</param>
    /// <returns>The detected layout, or null when the aspect matches none.</returns>
    public static CubeLayout? DetectCubeLayout(int width, int height)
        => width <= 0 || height <= 0 ? null
            : width == 2 * height ? CubeLayout.Panorama
            : width == 6 * height ? CubeLayout.HorizontalStrip
            : height == 6 * width ? CubeLayout.VerticalStrip
            : width * 3 == height * 4 ? CubeLayout.HorizontalCross
            : width * 4 == height * 3 ? CubeLayout.VerticalCross
            : null;

    /// <summary>Removes a cube face suffix from an image name.</summary>
    /// <param name="stem">The image file name without its extension.</param>
    /// <returns>The name shared by the six faces, or the stem when it names no face.</returns>
    public static string StripFaceSuffix(string stem)
    {
        ArgumentNullException.ThrowIfNull(stem);
        foreach (var suffix in TextureSource.CubeFaceSuffixSets.SelectMany(static suffixes => suffixes))
        {
            if (stem.Length > suffix.Length && stem.EndsWith(suffix, StringComparison.OrdinalIgnoreCase))
            {
                return stem[..^suffix.Length];
            }
        }

        return stem;
    }

    /// <summary>Creates an image texture descriptor under a project authoring mount.</summary>
    /// <param name="request">The selected image, destination, name and native cook settings.</param>
    /// <param name="cancellationToken">Cancels the copy before the descriptor is published.</param>
    /// <returns>The texture's identity, which the descriptor names and the cook pipeline cooks.</returns>
    /// <exception cref="TextureImportException">The request is refused; its code says why.</exception>
    public async Task<AssetPath> CreateAsync(TextureImportRequest request, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(request);
        cancellationToken.ThrowIfCancellationRequested();
        var target = this.ResolveTarget(request);
        if (this.IsTaken(target))
        {
            throw new TextureImportException(TextureImportCodes.NameTaken, $"A texture asset named '{request.Name}' already exists in this folder.");
        }

        // The descriptor is made first: settings it refuses leave no file behind.
        var descriptor = new TextureSource(Path.GetFileName(target.ImagePath))
        {
            Name = request.Name,
            VirtualPath = target.VirtualPath,
            Intent = request.Intent,
            ColorSpace = request.ColorSpace,
            Format = request.Format,
            Cube = CubeSettings(request.Cube),
        };
        var descriptorBytes = JsonSerializer.SerializeToUtf8Bytes(TextureSourceWriter.ToJson(descriptor), JsonOptions);
        _ = this.fileSystem.Directory.CreateDirectory(Path.GetDirectoryName(target.ImagePath)!);
        var sources = request.Cube?.Layout == CubeLayout.SixFaces ? this.FindCubeFaces(request.SourcePath)! : [request.SourcePath];
        var imagesCreated = 0;
        var descriptorCreated = false;
        try
        {
            for (var index = 0; index < sources.Count; ++index)
            {
                var source = this.fileSystem.FileStream.New(sources[index], FileMode.Open, FileAccess.Read, FileShare.Read, 65536, FileOptions.Asynchronous | FileOptions.SequentialScan);
                await using var sourceLifetime = source.ConfigureAwait(false);
                var image = this.fileSystem.FileStream.New(target.ImagePaths[index], FileMode.CreateNew, FileAccess.Write, FileShare.None, 65536, FileOptions.Asynchronous | FileOptions.SequentialScan);
                await using var imageLifetime = image.ConfigureAwait(false);
                ++imagesCreated;
                await source.CopyToAsync(image, cancellationToken).ConfigureAwait(false);
            }

            var output = this.fileSystem.FileStream.New(target.DescriptorPath, FileMode.CreateNew, FileAccess.Write, FileShare.None, 4096, FileOptions.Asynchronous | FileOptions.SequentialScan);
            await using (output.ConfigureAwait(false))
            {
                descriptorCreated = true;
                await output.WriteAsync(descriptorBytes, cancellationToken).ConfigureAwait(false);
            }

            return target.VirtualPath;
        }
        catch (OperationCanceledException)
        {
            this.CleanupOwnedFiles(target, descriptorCreated, imagesCreated);
            throw;
        }
        catch (IOException)
        {
            this.CleanupOwnedFiles(target, descriptorCreated, imagesCreated);
            throw;
        }
        catch (UnauthorizedAccessException)
        {
            this.CleanupOwnedFiles(target, descriptorCreated, imagesCreated);
            throw;
        }
    }

    /// <summary>Resolves where a request puts the texture, and checks the request.</summary>
    /// <param name="request">The import request.</param>
    /// <returns>The texture's identity, its images and its descriptor.</returns>
    /// <exception cref="TextureImportException">The request is refused; its code says why.</exception>
    public TextureImportTarget ResolveTarget(TextureImportRequest request)
        => TextureImportTarget.Resolve(this, request);

    /// <summary>Gets a value indicating whether the texture's descriptor or one of its images already exists.</summary>
    /// <param name="target">The resolved target.</param>
    /// <returns><see langword="true"/> when importing would replace a file.</returns>
    public bool IsTaken(TextureImportTarget target)
    {
        ArgumentNullException.ThrowIfNull(target);
        return target.ImagePaths.Any(this.fileSystem.File.Exists) || this.fileSystem.File.Exists(target.DescriptorPath);
    }

    /// <summary>Reads an image's width and height from its header, without decoding its pixels.</summary>
    /// <param name="path">A PNG, JPEG, BMP, TGA, Radiance HDR, OpenEXR, WebP or KTX2 image.</param>
    /// <returns>The dimensions, or <see langword="null"/> when the header cannot be read.</returns>
    public (int Width, int Height)? ReadDimensions(string path) => ImageDimensions.TryRead(this.fileSystem, path);

    /// <summary>Gets a value indicating whether an image exists.</summary>
    /// <param name="path">The image.</param>
    /// <returns><see langword="true"/> when the file exists.</returns>
    internal bool ImageExists(string path) => this.fileSystem.File.Exists(path);

    /// <summary>Finds the six face files of a cube from any one of them, named as the cooker expects.</summary>
    /// <param name="path">One face image, such as <c>sky_px.hdr</c>, <c>sky_posx.hdr</c> or <c>sky_right.hdr</c>.</param>
    /// <returns>The six existing faces in +X, -X, +Y, -Y, +Z, -Z order, or <see langword="null"/> when the set is incomplete.</returns>
    public IReadOnlyList<string>? FindCubeFaces(string path)
    {
        if (string.IsNullOrWhiteSpace(path))
        {
            return null;
        }

        var folder = Path.GetDirectoryName(Path.GetFullPath(path)) ?? string.Empty;
        var stem = Path.GetFileNameWithoutExtension(path);
        var extension = Path.GetExtension(path);
        foreach (var suffixes in TextureSource.CubeFaceSuffixSets)
        {
            var suffix = suffixes.FirstOrDefault(candidate => stem.Length > candidate.Length && stem.EndsWith(candidate, StringComparison.OrdinalIgnoreCase));
            if (suffix is null)
            {
                continue;
            }

            var stemBase = stem[..^suffix.Length];
            var faces = suffixes.Select(face => Path.Combine(folder, stemBase + face + extension)).ToArray();
            if (faces.All(this.fileSystem.File.Exists))
            {
                return faces;
            }
        }

        return null;
    }

    private static TextureCubeSettings? CubeSettings(TextureCubeImport? cube)
        => cube?.Layout switch
        {
            null => null,
            CubeLayout.Panorama => new() { EquirectToCube = true, FaceSize = cube.FaceSize },
            CubeLayout.HorizontalStrip => new() { Cubemap = true, Layout = "hstrip" },
            CubeLayout.VerticalStrip => new() { Cubemap = true, Layout = "vstrip" },
            CubeLayout.HorizontalCross => new() { Cubemap = true, Layout = "hcross" },
            CubeLayout.VerticalCross => new() { Cubemap = true, Layout = "vcross" },

            // The cooker finds the other faces from the +X face's name.
            CubeLayout.SixFaces => new() { Cubemap = true },
            _ => throw new ArgumentOutOfRangeException(nameof(cube), cube.Layout, "Unknown cube layout."),
        };

    private void CleanupOwnedFiles(TextureImportTarget target, bool descriptorCreated, int imagesCreated)
    {
        if (descriptorCreated)
        {
            this.fileSystem.File.Delete(target.DescriptorPath);
        }

        foreach (var image in target.ImagePaths.Take(imagesCreated))
        {
            this.fileSystem.File.Delete(image);
        }
    }
}
