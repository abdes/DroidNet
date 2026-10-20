// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Textures;

/// <summary>Where an import puts a texture: its identity, its images and its descriptor, inside an authoring mount.</summary>
/// <param name="MountName">The authoring mount that receives the texture.</param>
/// <param name="VirtualPath">The texture's identity.</param>
/// <param name="ImagePath">The image the descriptor names: the copied image, or the +X face of a six-face cube.</param>
/// <param name="DescriptorPath">The descriptor file.</param>
public sealed record TextureImportTarget(string MountName, AssetPath VirtualPath, string ImagePath, string DescriptorPath)
{
    /// <summary>Gets every image the import writes: the source image, or the six faces with the +X face first.</summary>
    public IReadOnlyList<string> ImagePaths { get; init; } = [ImagePath];

    /// <summary>Resolves and checks a reviewed texture import request.</summary>
    /// <param name="import">The import, which reads the selected images.</param>
    /// <param name="request">The import request.</param>
    /// <returns>The in-mount descriptor and image paths.</returns>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Globalization", "CA1308:Normalize strings to uppercase", Justification = "Copied images keep a lowercase extension; the text is a file name, not a comparison key.")]
    internal static TextureImportTarget Resolve(TextureImport import, TextureImportRequest request)
    {
        ValidateRequest(import, request);
        var (mount, physicalFolder) = ResolveFolder(request);
        var sixFaces = request.Cube?.Layout == CubeLayout.SixFaces;
        if (sixFaces)
        {
            ValidateCubeFaces(import, request.SourcePath);
        }
        else if (request.Cube is { } cube)
        {
            ValidateLayoutFits(import, request.SourcePath, cube.Layout);
        }

        var extension = Path.GetExtension(request.SourcePath).ToLowerInvariant();
        var imagePaths = (sixFaces ? TextureImport.FaceSuffixes : [string.Empty])
            .Select(suffix => Path.Combine(physicalFolder, request.Name + suffix + extension))
            .ToArray();
        var descriptorPath = Path.Combine(physicalFolder, request.Name + AssetFileTypes.TextureDescriptor);
        return new(mount.Name, request.DestinationFolder.Combine(request.Name + AssetFileTypes.Texture), imagePaths[0], descriptorPath) { ImagePaths = imagePaths };
    }

    // The image, the name and the cook settings, before the destination is looked at.
    private static void ValidateRequest(TextureImport import, TextureImportRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        ArgumentNullException.ThrowIfNull(request.Project, nameof(request));
        ArgumentNullException.ThrowIfNull(request.DestinationFolder, nameof(request));
        ArgumentException.ThrowIfNullOrWhiteSpace(request.SourcePath, nameof(request));
        if (!TextureImport.IsSupportedImage(request.SourcePath))
        {
            throw new TextureImportException(TextureImportCodes.ImageUnsupported, "Choose a PNG, JPEG, TGA, BMP or HDR image.");
        }

        if (!import.ImageExists(request.SourcePath))
        {
            throw new TextureImportException(TextureImportCodes.ImageMissing, "The selected image does not exist.");
        }

        if (Oxygen.Managed.Core.VirtualPath.ValidateSegment(request.Name + AssetFileTypes.TextureDescriptor, VirtualPathRuleSet.Authored) is { } invalidName)
        {
            throw new TextureImportException(TextureImportCodes.NameInvalid, invalidName.ToMessage());
        }

        if (!TextureImport.Intents.Contains(request.Intent, StringComparer.Ordinal)
            || !TextureImport.ColorSpaces.Contains(request.ColorSpace, StringComparer.Ordinal)
            || !TextureImport.Formats.Contains(request.Format, StringComparer.Ordinal))
        {
            throw new TextureImportException(TextureImportCodes.SettingsUnsupported, "Choose a supported texture intent, color space and output format.");
        }

        if (request.Cube is { Layout: CubeLayout.Panorama } panorama
            && (panorama.FaceSize is not { } faceSize || faceSize <= 0 || faceSize % 256 != 0))
        {
            throw new TextureImportException(TextureImportCodes.FaceSizeInvalid, "A panorama cube needs a face size that is a multiple of 256.");
        }
    }

    // The destination: an authoring mount, its folders below it, and the physical folder they name inside the mount.
    private static (ProjectMountPoint Mount, string PhysicalFolder) ResolveFolder(TextureImportRequest request)
    {
        var folder = request.DestinationFolder;
        var mount = request.Project.AuthoringMounts.FirstOrDefault(mount => string.Equals(mount.Name, folder.Mount, StringComparison.Ordinal))
            ?? throw new TextureImportException(TextureImportCodes.DestinationNotAuthoring, "Choose a folder inside a project authoring mount.");
        var folderParts = folder.IsMount ? [] : folder.RelativePath.Split('/');
        if (folderParts.Any(Oxygen.Managed.Core.VirtualPath.IsDerivedContent))
        {
            throw new TextureImportException(TextureImportCodes.DestinationDerived, "Choose an authoring folder outside source-media and generated folders.");
        }

        var location = request.Project.Find(folder)
            ?? throw new TextureImportException(TextureImportCodes.DestinationNotAuthoring, "Choose a folder inside a project authoring mount.");
        return (mount, location.File.FullPath);
    }

    // A strip or cross is cut into faces without resampling, so the image must be exactly the
    // grid of square faces; a panorama is resampled and fits any size.
    private static void ValidateLayoutFits(TextureImport import, string sourcePath, CubeLayout layout)
    {
        if (layout == CubeLayout.Panorama || import.ReadDimensions(sourcePath) is not { } size
            || TextureImport.DetectCubeLayout(size.Width, size.Height) == layout)
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
        throw new TextureImportException(
            TextureImportCodes.LayoutMismatch,
            string.Create(
                CultureInfo.InvariantCulture,
                $"{name} must be exactly {columns} square faces wide and {rows} high, with no margins (for example {columns * 256} × {rows * 256}); this image is {size.Width} × {size.Height}."));
    }

    private static void ValidateCubeFaces(TextureImport import, string sourcePath)
    {
        var faces = import.FindCubeFaces(sourcePath)
            ?? throw new TextureImportException(
                TextureImportCodes.CubeFacesIncomplete,
                "Six-face import needs six images next to the selected one, named with _px, _nx, _py, _ny, _pz and _nz (or _posx... or _right, _left, _top, _bottom, _front, _back).");
        var sizes = faces.Select(import.ReadDimensions).ToArray();
        if (sizes.Any(static size => size is null))
        {
            throw new TextureImportException(TextureImportCodes.CubeFaceUnreadable, "A cube face image could not be read.");
        }

        var first = sizes[0]!.Value;
        if (first.Width != first.Height || sizes.Any(size => size!.Value != first))
        {
            throw new TextureImportException(TextureImportCodes.CubeFacesMismatched, "Cube faces must be square images of the same size.");
        }
    }
}
