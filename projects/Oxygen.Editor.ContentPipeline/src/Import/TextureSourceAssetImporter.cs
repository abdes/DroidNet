// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Text.Json;
using System.Text.Json.Serialization;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Creates a named, project-owned texture descriptor and its source image.</summary>
public static class TextureSourceAssetImporter
{
    private static readonly HashSet<string> ImageExtensions = new(AssetFileTypes.Suffixes(AssetFileCategory.Image), StringComparer.OrdinalIgnoreCase);

    private static readonly HashSet<string> Intents = new(StringComparer.Ordinal)
    {
        "albedo", "normal", "roughness", "metallic", "ao", "orm", "emissive", "opacity", "data", "height", "hdr_env", "hdr_probe",
    };

    private static readonly HashSet<string> ColorSpaces = new(StringComparer.Ordinal)
    {
        "srgb", "linear",
    };

    private static readonly HashSet<string> Formats = new(StringComparer.Ordinal)
    {
        "rgba8", "rgba8_srgb", "rgba8-srgb", "bc7", "bc7_srgb", "bc7-srgb", "rgba16f", "rgba32f",
    };

    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };

    // The cooker's face naming conventions, each in +X, -X, +Y, -Y, +Z, -Z order. The first
    // set also names the faces an import writes.
    private static readonly string[][] FaceSuffixSets =
    [
        ["_px", "_nx", "_py", "_ny", "_pz", "_nz"],
        ["_posx", "_negx", "_posy", "_negy", "_posz", "_negz"],
        ["_right", "_left", "_top", "_bottom", "_front", "_back"],
    ];

    /// <summary>Gets the supported standalone image extensions.</summary>
    public static IReadOnlyCollection<string> SupportedExtensions => ImageExtensions;

    /// <summary>Gets the suffixes an imported six-face cube gives its face files, in +X, -X, +Y, -Y, +Z, -Z order.</summary>
    public static IReadOnlyList<string> FaceSuffixes => FaceSuffixSets[0];

    /// <summary>Creates an image texture descriptor under a project authoring mount.</summary>
    /// <param name="request">The selected image, destination, name and native cook settings.</param>
    /// <param name="cancellationToken">Cancels the copy before the descriptor is published.</param>
    /// <returns>The authored descriptor asset URI consumed by the normal cook pipeline.</returns>
    public static async Task<AssetPath> CreateAsync(TextureSourceImportRequest request, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(request);
        cancellationToken.ThrowIfCancellationRequested();
        var target = TextureSourceImportTarget.Resolve(request);
        if (target.ImagePaths.Any(File.Exists) || File.Exists(target.DescriptorPath))
        {
            throw new IOException($"A texture asset named '{request.Name}' already exists in this folder.");
        }

        Directory.CreateDirectory(Path.GetDirectoryName(target.ImagePath)!);
        var sources = request.Cube?.Layout == CubeLayout.SixFaces ? FindCubeFaces(request.SourcePath)! : [request.SourcePath];
        var imagesCreated = 0;
        var descriptorCreated = false;
        try
        {
            for (var index = 0; index < sources.Count; ++index)
            {
                var source = new FileStream(sources[index], FileMode.Open, FileAccess.Read, FileShare.Read, 65536, FileOptions.Asynchronous | FileOptions.SequentialScan);
                await using var sourceLifetime = source.ConfigureAwait(false);
                var image = new FileStream(target.ImagePaths[index], FileMode.CreateNew, FileAccess.Write, FileShare.None, 65536, FileOptions.Asynchronous | FileOptions.SequentialScan);
                await using var imageLifetime = image.ConfigureAwait(false);
                ++imagesCreated;
                await source.CopyToAsync(image, cancellationToken).ConfigureAwait(false);
            }

            var descriptor = new TextureDescriptor(
                request.Name,
                target.VirtualPath,
                Path.GetFileName(target.ImagePath),
                request.Intent,
                request.ColorSpace,
                request.Format,
                CubeSettings.From(request.Cube));
            var descriptorBytes = JsonSerializer.SerializeToUtf8Bytes(descriptor, JsonOptions);
            var output = new FileStream(target.DescriptorPath, FileMode.CreateNew, FileAccess.Write, FileShare.None, 4096, FileOptions.Asynchronous | FileOptions.SequentialScan);
            await using (output.ConfigureAwait(false))
            {
                descriptorCreated = true;
                await output.WriteAsync(descriptorBytes, cancellationToken).ConfigureAwait(false);
            }

            return target.AssetUri;
        }
        catch (OperationCanceledException)
        {
            CleanupOwnedFiles(target, descriptorCreated, imagesCreated);
            throw;
        }
        catch (IOException)
        {
            CleanupOwnedFiles(target, descriptorCreated, imagesCreated);
            throw;
        }
        catch (UnauthorizedAccessException)
        {
            CleanupOwnedFiles(target, descriptorCreated, imagesCreated);
            throw;
        }
    }

    /// <summary>Returns whether a path extension is supported by the standalone image import action.</summary>
    /// <param name="path">The selected image path.</param>
    /// <returns>True for supported source image formats.</returns>
    public static bool IsSupportedImage(string path)
        => !string.IsNullOrWhiteSpace(path) && ImageExtensions.Contains(Path.GetExtension(path));

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

    /// <summary>Gets whether a texture descriptor cooks a cube texture.</summary>
    /// <param name="descriptorPath">The texture descriptor file.</param>
    /// <returns><see langword="true"/> when its cube settings produce a cube texture.</returns>
    public static bool IsCubeDescriptor(string descriptorPath) => ReadCubeDescriptor(descriptorPath) is not null;

    /// <summary>Reads what a cube texture descriptor cooks.</summary>
    /// <param name="descriptorPath">The texture descriptor file.</param>
    /// <returns>The cube's storage, or <see langword="null"/> when the descriptor cooks no cube texture.</returns>
    public static CubeDescriptorInfo? ReadCubeDescriptor(string descriptorPath)
    {
        try
        {
            using var stream = File.OpenRead(descriptorPath);
            using var document = JsonDocument.Parse(stream);
            var root = document.RootElement;
            if (!root.TryGetProperty("cube", out var cube) || cube.ValueKind != JsonValueKind.Object
                || !(IsTrue(cube, "cubemap") || IsTrue(cube, "equirect_to_cube")
                    || (cube.TryGetProperty("cube_layout", out var layout) && layout.ValueKind == JsonValueKind.String)))
            {
                return null;
            }

            var format = root.TryGetProperty("output", out var output) && output.ValueKind == JsonValueKind.Object
                && output.TryGetProperty("format", out var value) && value.ValueKind == JsonValueKind.String
                ? value.GetString()
                : null;
            return new CubeDescriptorInfo(StoresRadiance: format is "rgba16f" or "rgba32f");
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or JsonException)
        {
            return null;
        }

        static bool IsTrue(JsonElement cube, string name)
            => cube.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.True;
    }

    /// <summary>Checks whether a texture descriptor names a source image that exists beside it.</summary>
    /// <param name="descriptorPath">The texture descriptor file.</param>
    /// <returns><see langword="true"/> when the descriptor's <c>source</c> is a relative path inside its folder and the file exists.</returns>
    public static bool HasSourceImage(string descriptorPath)
    {
        try
        {
            using var descriptor = JsonDocument.Parse(File.ReadAllBytes(descriptorPath));
            if (!descriptor.RootElement.TryGetProperty("source", out var sourceValue)
                || sourceValue.ValueKind != JsonValueKind.String
                || string.IsNullOrWhiteSpace(sourceValue.GetString()))
            {
                return false;
            }

            var source = sourceValue.GetString()!;
            if (Path.IsPathRooted(source) || source.Replace('\\', '/').Split('/').Any(static segment => segment is ".." or "." or ""))
            {
                return false;
            }

            var directory = FileSystemPath.From(descriptorPath).Parent!;
            var sourcePath = directory.Combine(source);
            return sourcePath != directory && sourcePath.IsSameOrInside(directory) && File.Exists(sourcePath.FullPath);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or JsonException or ArgumentException)
        {
            return false;
        }
    }

    /// <summary>Finds the six face files of a cube from any one of them, named as the cooker expects.</summary>
    /// <param name="path">One face image, such as <c>sky_px.hdr</c>, <c>sky_posx.hdr</c> or <c>sky_right.hdr</c>.</param>
    /// <returns>The six existing faces in +X, -X, +Y, -Y, +Z, -Z order, or <see langword="null"/> when the set is incomplete.</returns>
    public static IReadOnlyList<string>? FindCubeFaces(string path)
    {
        if (string.IsNullOrWhiteSpace(path))
        {
            return null;
        }

        var folder = Path.GetDirectoryName(Path.GetFullPath(path)) ?? string.Empty;
        var stem = Path.GetFileNameWithoutExtension(path);
        var extension = Path.GetExtension(path);
        foreach (var suffixes in FaceSuffixSets)
        {
            var suffix = suffixes.FirstOrDefault(candidate => stem.Length > candidate.Length && stem.EndsWith(candidate, StringComparison.OrdinalIgnoreCase));
            if (suffix is null)
            {
                continue;
            }

            var stemBase = stem[..^suffix.Length];
            var faces = suffixes.Select(face => Path.Combine(folder, stemBase + face + extension)).ToArray();
            if (faces.All(File.Exists))
            {
                return faces;
            }
        }

        return null;
    }

    /// <summary>Removes a cube face suffix from an image name.</summary>
    /// <param name="stem">The image file name without its extension.</param>
    /// <returns>The name shared by the six faces, or the stem when it names no face.</returns>
    public static string StripFaceSuffix(string stem)
    {
        ArgumentNullException.ThrowIfNull(stem);
        foreach (var suffix in FaceSuffixSets.SelectMany(static suffixes => suffixes))
        {
            if (stem.Length > suffix.Length && stem.EndsWith(suffix, StringComparison.OrdinalIgnoreCase))
            {
                return stem[..^suffix.Length];
            }
        }

        return stem;
    }

    internal static bool IsSupportedIntent(string value) => Intents.Contains(value);

    internal static bool IsSupportedColorSpace(string value) => ColorSpaces.Contains(value);

    internal static bool IsSupportedFormat(string value) => Formats.Contains(value);

    private static void CleanupOwnedFiles(TextureSourceImportTarget target, bool descriptorCreated, int imagesCreated)
    {
        if (descriptorCreated)
        {
            File.Delete(target.DescriptorPath);
        }

        foreach (var image in target.ImagePaths.Take(imagesCreated))
        {
            File.Delete(image);
        }
    }

    private sealed record TextureDescriptor(
        [property: JsonPropertyName("name")] string Name,
        [property: JsonPropertyName("virtual_path")] string VirtualPath,
        [property: JsonPropertyName("source")] string Source,
        [property: JsonPropertyName("intent")] string Intent,
        [property: JsonPropertyName("decode")] DecodeSettings Decode,
        [property: JsonPropertyName("output")] OutputSettings Output,
        [property: JsonPropertyName("cube"), JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] CubeSettings? Cube)
    {
        public TextureDescriptor(string name, string virtualPath, string source, string intent, string colorSpace, string format, CubeSettings? cube)
            : this(name, virtualPath, source, intent, new DecodeSettings(colorSpace), new OutputSettings(format), cube)
        {
        }
    }

    private sealed record CubeSettings(
        [property: JsonPropertyName("cubemap"), JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] bool? Cubemap,
        [property: JsonPropertyName("equirect_to_cube"), JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] bool? EquirectToCube,
        [property: JsonPropertyName("cube_face_size"), JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] int? FaceSize,
        [property: JsonPropertyName("cube_layout"), JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] string? Layout)
    {
        public static CubeSettings? From(TextureCubeImport? cube)
            => cube?.Layout switch
            {
                null => null,
                CubeLayout.Panorama => new(Cubemap: null, EquirectToCube: true, cube.FaceSize, Layout: null),
                CubeLayout.HorizontalStrip => new(Cubemap: true, EquirectToCube: null, FaceSize: null, "hstrip"),
                CubeLayout.VerticalStrip => new(Cubemap: true, EquirectToCube: null, FaceSize: null, "vstrip"),
                CubeLayout.HorizontalCross => new(Cubemap: true, EquirectToCube: null, FaceSize: null, "hcross"),
                CubeLayout.VerticalCross => new(Cubemap: true, EquirectToCube: null, FaceSize: null, "vcross"),

                // The cooker finds the other faces from the +X face's name.
                CubeLayout.SixFaces => new(Cubemap: true, EquirectToCube: null, FaceSize: null, Layout: null),
                _ => throw new ArgumentOutOfRangeException(nameof(cube), cube.Layout, "Unknown cube layout."),
            };
    }

    private sealed record DecodeSettings([property: JsonPropertyName("color_space")] string ColorSpace);

    private sealed record OutputSettings([property: JsonPropertyName("format")] string Format);
}
