// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Serialization;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Creates a named, project-owned texture descriptor and its source image.</summary>
public static class TextureSourceAssetImporter
{
    private static readonly HashSet<string> ImageExtensions = new(StringComparer.OrdinalIgnoreCase)
    {
        ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr",
    };

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

    internal static bool IsSupportedIntent(string value) => Intents.Contains(value);

    internal static bool IsSupportedColorSpace(string value) => ColorSpaces.Contains(value);

    internal static bool IsSupportedFormat(string value) => Formats.Contains(value);

    /// <summary>Gets the supported standalone image extensions.</summary>
    public static IReadOnlyCollection<string> SupportedExtensions => ImageExtensions;

    /// <summary>Creates an image texture descriptor under a project authoring mount.</summary>
    /// <param name="request">The selected image, destination, name and native cook settings.</param>
    /// <param name="cancellationToken">Cancels the copy before the descriptor is published.</param>
    /// <returns>The authored descriptor asset URI consumed by the normal cook pipeline.</returns>
    public static async Task<Uri> CreateAsync(TextureSourceImportRequest request, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(request);
        cancellationToken.ThrowIfCancellationRequested();
        var target = TextureSourceImportTarget.Resolve(request);
        if (File.Exists(target.ImagePath) || File.Exists(target.DescriptorPath))
        {
            throw new IOException($"A texture asset named '{request.Name}' already exists in this folder.");
        }

        Directory.CreateDirectory(Path.GetDirectoryName(target.ImagePath)!);
        var imageCreated = false;
        var descriptorCreated = false;
        try
        {
            await using (var source = new FileStream(request.SourcePath, FileMode.Open, FileAccess.Read, FileShare.Read, 65536, FileOptions.Asynchronous | FileOptions.SequentialScan))
            await using (var image = new FileStream(target.ImagePath, FileMode.CreateNew, FileAccess.Write, FileShare.None, 65536, FileOptions.Asynchronous | FileOptions.SequentialScan))
            {
                imageCreated = true;
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
            await using (var output = new FileStream(target.DescriptorPath, FileMode.CreateNew, FileAccess.Write, FileShare.None, 4096, FileOptions.Asynchronous | FileOptions.SequentialScan))
            {
                descriptorCreated = true;
                await output.WriteAsync(descriptorBytes, cancellationToken).ConfigureAwait(false);
            }

            return target.AssetUri;
        }
        catch (OperationCanceledException)
        {
            CleanupOwnedFiles(target, descriptorCreated, imageCreated);
            throw;
        }
        catch (IOException)
        {
            CleanupOwnedFiles(target, descriptorCreated, imageCreated);
            throw;
        }
        catch (UnauthorizedAccessException)
        {
            CleanupOwnedFiles(target, descriptorCreated, imageCreated);
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
    public static bool IsCubeDescriptor(string descriptorPath)
    {
        try
        {
            using var stream = File.OpenRead(descriptorPath);
            using var document = JsonDocument.Parse(stream);
            if (!document.RootElement.TryGetProperty("cube", out var cube) || cube.ValueKind != JsonValueKind.Object)
            {
                return false;
            }

            return IsTrue(cube, "cubemap") || IsTrue(cube, "equirect_to_cube")
                || (cube.TryGetProperty("cube_layout", out var layout) && layout.ValueKind == JsonValueKind.String);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or JsonException)
        {
            return false;
        }

        static bool IsTrue(JsonElement cube, string name)
            => cube.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.True;
    }

    private static void CleanupOwnedFiles(TextureSourceImportTarget target, bool descriptorCreated, bool imageCreated)
    {
        if (descriptorCreated)
        {
            File.Delete(target.DescriptorPath);
        }

        if (imageCreated)
        {
            File.Delete(target.ImagePath);
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
                _ => throw new ArgumentOutOfRangeException(nameof(cube), cube.Layout, "Unknown cube layout."),
            };
    }

    private sealed record DecodeSettings([property: JsonPropertyName("color_space")] string ColorSpace);

    private sealed record OutputSettings([property: JsonPropertyName("format")] string Format);
}

/// <summary>A user's reviewed image-to-texture import request.</summary>
/// <param name="Project">The project whose authoring mount receives the texture.</param>
/// <param name="SourcePath">The selected image file.</param>
/// <param name="DestinationFolder">The selected authoring folder URI.</param>
/// <param name="Name">The unique texture asset name without file extensions.</param>
/// <param name="Intent">The native texture semantic.</param>
/// <param name="ColorSpace">The native decode color space.</param>
/// <param name="Format">The native output format.</param>
public sealed record TextureSourceImportRequest(
    ProjectContext Project,
    string SourcePath,
    Uri DestinationFolder,
    string Name,
    string Intent,
    string ColorSpace,
    string Format)
{
    /// <summary>Gets how the image becomes a cube texture, or null for a 2D texture.</summary>
    public TextureCubeImport? Cube { get; init; }
}

/// <summary>How a single image lays out the six faces of a cube texture.</summary>
public enum CubeLayout
{
    /// <summary>A 2:1 equirectangular (latitude-longitude) panorama, resampled to faces.</summary>
    Panorama,

    /// <summary>Six faces side by side, 6:1.</summary>
    HorizontalStrip,

    /// <summary>Six faces stacked, 1:6.</summary>
    VerticalStrip,

    /// <summary>A 4:3 horizontal cross.</summary>
    HorizontalCross,

    /// <summary>A 3:4 vertical cross.</summary>
    VerticalCross,
}

/// <summary>Cube import settings for a single source image.</summary>
/// <param name="Layout">How the image lays out the faces.</param>
/// <param name="FaceSize">The face size in pixels for a panorama, a multiple of 256; ignored otherwise.</param>
public sealed record TextureCubeImport(CubeLayout Layout, int? FaceSize = null);

/// <summary>A collision-checked filesystem target for a named texture asset.</summary>
public sealed record TextureSourceImportTarget(string MountName, string VirtualPath, string ImagePath, string DescriptorPath, Uri AssetUri)
{
    /// <summary>Resolves and validates a reviewed texture import request.</summary>
    /// <param name="request">The import request.</param>
    /// <returns>The in-mount descriptor and source paths.</returns>
    public static TextureSourceImportTarget Resolve(TextureSourceImportRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        ArgumentNullException.ThrowIfNull(request.Project);
        ArgumentNullException.ThrowIfNull(request.DestinationFolder);
        ArgumentException.ThrowIfNullOrWhiteSpace(request.SourcePath);
        if (!TextureSourceAssetImporter.IsSupportedImage(request.SourcePath) || !File.Exists(request.SourcePath))
        {
            throw new ArgumentException("Choose an existing PNG, JPEG, TGA, BMP or HDR image.", nameof(request));
        }

        if (string.IsNullOrWhiteSpace(request.Name)
            || request.Name is "." or ".."
            || request.Name.EndsWith('.')
            || request.Name.EndsWith(' ')
            || request.Name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0)
        {
            throw new ArgumentException("Use a valid texture asset name.", nameof(request));
        }

        if (!TextureSourceAssetImporter.IsSupportedIntent(request.Intent)
            || !TextureSourceAssetImporter.IsSupportedColorSpace(request.ColorSpace)
            || !TextureSourceAssetImporter.IsSupportedFormat(request.Format))
        {
            throw new ArgumentException("Choose a supported texture intent, color space and output format.", nameof(request));
        }

        if (request.Cube is { Layout: CubeLayout.Panorama } panorama
            && (panorama.FaceSize is not { } faceSize || faceSize <= 0 || faceSize % 256 != 0))
        {
            throw new ArgumentException("A panorama cube needs a face size that is a multiple of 256.", nameof(request));
        }

        var folder = request.DestinationFolder;
        if (!folder.IsAbsoluteUri || !string.Equals(folder.Scheme, AssetUris.Scheme, StringComparison.OrdinalIgnoreCase)
            || !string.IsNullOrEmpty(folder.Host) || !string.IsNullOrEmpty(folder.Query) || !string.IsNullOrEmpty(folder.Fragment))
        {
            throw new ArgumentException("Choose a project authoring folder.", nameof(request));
        }

        var parts = Uri.UnescapeDataString(folder.AbsolutePath).Trim('/').Split('/');
        if (parts.Length == 0 || parts.Any(static part => part is "" or "." or ".." || part.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0))
        {
            throw new ArgumentException("Choose a valid project authoring folder.", nameof(request));
        }

        var mount = request.Project.AuthoringMounts.FirstOrDefault(mount => string.Equals(mount.Name, parts[0], StringComparison.OrdinalIgnoreCase))
            ?? throw new ArgumentException("Choose a folder inside a project authoring mount.", nameof(request));
        if (parts.Skip(1).Any(static part => part.Equals("SourceMedia", StringComparison.OrdinalIgnoreCase)
            || part.Equals(".cooked", StringComparison.OrdinalIgnoreCase) || part.Equals(".imported", StringComparison.OrdinalIgnoreCase)
            || part.Equals(".build", StringComparison.OrdinalIgnoreCase) || part.Equals(".pipeline", StringComparison.OrdinalIgnoreCase)))
        {
            throw new ArgumentException("Choose an authoring folder outside source-media and generated folders.", nameof(request));
        }

        var mountRoot = Path.GetFullPath(Path.Combine(request.Project.ProjectRoot, mount.RelativePath));
        var relativeFolder = Path.Combine(parts.Skip(1).ToArray());
        var physicalFolder = Path.GetFullPath(Path.Combine(mountRoot, relativeFolder));
        if (!IsUnderRoot(physicalFolder, mountRoot))
        {
            throw new ArgumentException("The selected folder resolves outside its authoring mount.", nameof(request));
        }

        var imageName = request.Name + Path.GetExtension(request.SourcePath).ToLowerInvariant();
        var descriptorName = request.Name + ".otex.json";
        var imagePath = Path.Combine(physicalFolder, imageName);
        var descriptorPath = Path.Combine(physicalFolder, descriptorName);
        var virtualDirectory = string.Join('/', parts.Skip(1));
        var virtualPath = "/" + mount.Name + (virtualDirectory.Length == 0 ? string.Empty : "/" + virtualDirectory) + "/" + request.Name + ".otex";
        var assetPath = string.Join('/', new[] { mount.Name }.Concat(parts.Skip(1)).Append(descriptorName).Select(Uri.EscapeDataString));
        return new(mount.Name, virtualPath, imagePath, descriptorPath, new Uri(AssetUris.Scheme + ":///" + assetPath));
    }

    private static bool IsUnderRoot(string path, string root)
    {
        var normalizedRoot = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar) + Path.DirectorySeparatorChar;
        var normalizedPath = Path.GetFullPath(path).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar) + Path.DirectorySeparatorChar;
        return normalizedPath.StartsWith(normalizedRoot, StringComparison.OrdinalIgnoreCase);
    }
}
