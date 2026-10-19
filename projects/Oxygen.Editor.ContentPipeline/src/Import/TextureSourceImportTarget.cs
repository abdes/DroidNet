// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>A collision-checked filesystem target for a named texture asset.</summary>
public sealed record TextureSourceImportTarget(string MountName, AssetPath VirtualPath, string ImagePath, string DescriptorPath, AssetPath AssetUri)
{
    /// <summary>Gets every image the import writes: the source image, or the six faces with the +X face first.</summary>
    public IReadOnlyList<string> ImagePaths { get; init; } = [ImagePath];

    /// <summary>Resolves and validates a reviewed texture import request.</summary>
    /// <param name="request">The import request.</param>
    /// <returns>The in-mount descriptor and source paths.</returns>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Globalization", "CA1308:Normalize strings to uppercase", Justification = "Copied images keep a lowercase extension; the text is a file name, not a comparison key.")]
    public static TextureSourceImportTarget Resolve(TextureSourceImportRequest request)
    {
        ValidateRequest(request);
        var (mount, folderParts, physicalFolder) = ResolveFolder(request);
        var sixFaces = request.Cube?.Layout == CubeLayout.SixFaces;
        if (sixFaces)
        {
            ValidateCubeFaces(request.SourcePath);
        }
        else if (request.Cube is { } cube)
        {
            ValidateLayoutFits(request.SourcePath, cube.Layout);
        }

        var extension = Path.GetExtension(request.SourcePath).ToLowerInvariant();
        var imagePaths = (sixFaces ? TextureSourceAssetImporter.FaceSuffixes : [string.Empty])
            .Select(suffix => Path.Combine(physicalFolder, request.Name + suffix + extension))
            .ToArray();
        var descriptorName = request.Name + AssetFileTypes.TextureDescriptor;
        var descriptorPath = Path.Combine(physicalFolder, descriptorName);
        var folder = request.DestinationFolder;
        return new(mount.Name, folder.Combine(request.Name + AssetFileTypes.Texture), imagePaths[0], descriptorPath, folder.Combine(descriptorName)) { ImagePaths = imagePaths };
    }

    // The image, the name and the cook settings, before the destination is looked at.
    private static void ValidateRequest(TextureSourceImportRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        ArgumentNullException.ThrowIfNull(request.Project, nameof(request));
        ArgumentNullException.ThrowIfNull(request.DestinationFolder, nameof(request));
        ArgumentException.ThrowIfNullOrWhiteSpace(request.SourcePath, nameof(request));
        if (!TextureSourceAssetImporter.IsSupportedImage(request.SourcePath) || !File.Exists(request.SourcePath))
        {
            throw new ArgumentException("Choose an existing PNG, JPEG, TGA, BMP or HDR image.", nameof(request));
        }

        if (Oxygen.Managed.Core.VirtualPath.ValidateSegment(request.Name + AssetFileTypes.TextureDescriptor, VirtualPathRuleSet.Authored) is { } invalidName)
        {
            throw new ArgumentException(invalidName.ToMessage(), nameof(request));
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
    }

    // The destination: an authoring mount, its folders below it, and the physical folder they name inside the mount.
    private static (ProjectMountPoint Mount, string[] FolderParts, string PhysicalFolder) ResolveFolder(TextureSourceImportRequest request)
    {
        var folder = request.DestinationFolder;
        var folderParts = folder.IsMount ? [] : folder.RelativePath.Split('/');

        var mount = request.Project.AuthoringMounts.FirstOrDefault(mount => string.Equals(mount.Name, folder.Mount, StringComparison.Ordinal))
            ?? throw new ArgumentException("Choose a folder inside a project authoring mount.", nameof(request));
        if (folderParts.Any(Oxygen.Managed.Core.VirtualPath.IsDerivedContent))
        {
            throw new ArgumentException("Choose an authoring folder outside source-media and generated folders.", nameof(request));
        }

        var mountRoot = request.Project.ProjectRoot.Combine(mount.RelativePath);
        var physicalFolder = folderParts.Length == 0 ? mountRoot : mountRoot.Combine(Path.Combine(folderParts));
        return physicalFolder.IsSameOrInside(mountRoot)
            ? (mount, folderParts, physicalFolder.FullPath)
            : throw new ArgumentException("The selected folder resolves outside its authoring mount.", nameof(request));
    }

    // A strip or cross is cut into faces without resampling, so the image must be exactly the
    // grid of square faces; a panorama is resampled and fits any size.
    private static void ValidateLayoutFits(string sourcePath, CubeLayout layout)
    {
        if (layout == CubeLayout.Panorama || ImageDimensions.TryRead(sourcePath) is not { } size
            || TextureSourceAssetImporter.DetectCubeLayout(size.Width, size.Height) == layout)
        {
            return;
        }

        var (columns, rows, name) = layout switch
        {
            CubeLayout.HorizontalStrip => (6, 1, "A horizontal strip"),
            CubeLayout.VerticalStrip => (1, 6, "A vertical strip"),
            CubeLayout.HorizontalCross => (4, 3, "A horizontal cross"),
            _ => (3, 4, "A vertical cross"),
        };
        throw new ArgumentException(
            string.Create(
                CultureInfo.InvariantCulture,
                $"{name} must be exactly {columns} square faces wide and {rows} high, with no margins (for example {columns * 256} × {rows * 256}); this image is {size.Width} × {size.Height}."),
            nameof(sourcePath));
    }

    private static void ValidateCubeFaces(string sourcePath)
    {
        var faces = TextureSourceAssetImporter.FindCubeFaces(sourcePath)
            ?? throw new ArgumentException(
                "Six-face import needs six images next to the selected one, named with _px, _nx, _py, _ny, _pz and _nz (or _posx... or _right, _left, _top, _bottom, _front, _back).",
                nameof(sourcePath));
        var sizes = faces.Select(ImageDimensions.TryRead).ToArray();
        if (sizes.Any(static size => size is null))
        {
            throw new ArgumentException("A cube face image could not be read.", nameof(sourcePath));
        }

        var first = sizes[0]!.Value;
        if (first.Width != first.Height || sizes.Any(size => size!.Value != first))
        {
            throw new ArgumentException("Cube faces must be square images of the same size.", nameof(sourcePath));
        }
    }
}
