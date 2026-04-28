// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.IO.Abstractions;
using System.Text.Json;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Utils;

namespace Oxygen.Editor.ProjectBrowser.Templates;

/// <summary>
/// Provides functionality to load project templates from the local filesystem.
/// </summary>
/// <param name="filesystem">The filesystem abstraction used for file operations.</param>
/// <remarks>
/// <para>
/// LocalTemplatesSource implements <see cref="ITemplatesSource"/> to handle loading and validation
/// of project templates stored as JSON files on the local filesystem. It processes template
/// descriptors and validates associated assets like icons and preview images.
/// </para>
/// <para>
/// The class uses <see cref="System.IO.Abstractions.IFileSystem"/> for filesystem operations,
/// making it testable and mockable.
/// </para>
/// </remarks>
/// <example>
/// <para><strong>Example Usage</strong></para>
/// <![CDATA[
/// // Initialize with real filesystem
/// var source = new LocalTemplatesSource(new FileSystem());
///
/// // Load a template from local file
/// try
/// {
///     var templateUri = new Uri("file:///C:/Templates/WebApp/template.json");
///     var template = await source.LoadTemplateAsync(templateUri);
///
///     // Template assets are now validated and paths are absolute
///     Console.WriteLine($"Template: {template.Name}");
///     Console.WriteLine($"Icon: {template.Icon}");
///     Console.WriteLine($"Preview images: {string.Join(", ", template.PreviewImages)}");
/// }
/// catch (TemplateLoadingException ex)
/// {
///     Console.WriteLine($"Failed to load template: {ex.Message}");
/// }
/// ]]>
/// </example>
public class LocalTemplatesSource(IFileSystem filesystem) : ITemplatesSource
{
    private static readonly string[] RequiredTemplateFolders =
    [
        "Content",
        "Content/Scenes",
        "Content/Materials",
        "Content/Geometry",
        "Content/Textures",
        "Content/Audio",
        "Content/Video",
        "Content/Scripts",
        "Content/Prefabs",
        "Content/Animations",
        "Content/SourceMedia",
        "Content/SourceMedia/Images",
        "Content/SourceMedia/Audio",
        "Content/SourceMedia/Video",
        "Content/SourceMedia/DCC",
        "Config",
    ];

    private readonly JsonSerializerOptions jsonSerializerOptions = new()
    {
        AllowTrailingCommas = true,
        Converters = { new CategoryJsonConverter() },
    };

    /// <summary>
    /// Gets whether this source can load templates from the specified URI.
    /// </summary>
    /// <param name="fromUri">The URI to check.</param>
    /// <returns><see langword="true"/> if the URI uses the <c>file://</c> scheme; <see langword="false"/> otherwise.</returns>
    /// <remarks>
    /// <see cref="LocalTemplatesSource"/> only supports locations with a URI scheme <see cref="Uri.UriSchemeFile"/>.
    /// </remarks>
    public bool CanLoad(Uri fromUri) => string.Equals(fromUri.Scheme, Uri.UriSchemeFile, StringComparison.OrdinalIgnoreCase);

    /// <summary>
    /// Loads a template from a local filesystem location.
    /// </summary>
    /// <param name="fromUri">The URI pointing to the template descriptor JSON file.</param>
    /// <returns>The loaded and validated template information.</returns>
    /// <exception cref="ArgumentException">Thrown when the URI scheme is not <c>file://</c>.</exception>
    /// <exception cref="TemplateLoadingException">
    /// Thrown when:
    /// - The template file does not exist
    /// - The JSON content is invalid
    /// - Required assets (icons, preview images) are missing
    /// - Any other IO or parsing error occurs.
    /// </exception>
    public async Task<ITemplateInfo> LoadTemplateAsync(Uri fromUri)
    {
        if (!this.CanLoad(fromUri))
        {
            throw new ArgumentException(
                $"{nameof(LocalTemplatesSource)} can only load templates with a '{Uri.UriSchemeFile}' URI scheme. The given URI used the '{fromUri.Scheme}'.",
                nameof(fromUri));
        }

        var templatePath = fromUri.LocalPath;
        if (!filesystem.File.Exists(fromUri.LocalPath))
        {
            throw new TemplateLoadingException(
                $"Location `{templatePath}` does not correspond to an existing template descriptor file.");
        }

        Debug.WriteLine($"Loading local project template info from `{templatePath}`");

        try
        {
            var json = await filesystem.File.ReadAllTextAsync(templatePath, CancellationToken.None)
                .ConfigureAwait(true);

            var templateInfo = JsonSerializer.Deserialize<TemplateInfo>(json, this.jsonSerializerOptions);
            Debug.Assert(templateInfo != null, $"Json content of template at `{templatePath}` is not valid");
            if (templateInfo is null)
            {
                throw new TemplateLoadingException($"Template descriptor `{templatePath}` is invalid.");
            }

            // Update the paths for the icon and preview images to become absolute
            templateInfo.Location = filesystem.Path.GetFullPath(filesystem.Path.GetDirectoryName(templatePath)!);
            this.ValidateDescriptor(templateInfo, templateInfo.Location);
            if (templateInfo.Icon != null)
            {
                var iconPath = filesystem.Path.Combine(templateInfo.Location, templateInfo.Icon);
                if (!filesystem.File.Exists(iconPath))
                {
                    throw new TemplateLoadingException(
                        $"Missing icon file `{templateInfo.Icon}` at the template location.");
                }

                templateInfo.Icon = iconPath;
            }

            for (var index = 0; index < templateInfo.PreviewImages.Count; index++)
            {
                var previewImagePath = filesystem.Path.Combine(templateInfo.Location, templateInfo.PreviewImages[index]);
                if (!filesystem.File.Exists(previewImagePath))
                {
                    throw new TemplateLoadingException(
                        $"Missing preview image file `{previewImagePath}` at the template location.");
                }

                templateInfo.PreviewImages[index] = previewImagePath;
            }

            this.ValidatePayload(templateInfo);
            return templateInfo;
        }
        catch (Exception ex)
        {
            throw new TemplateLoadingException($"Could not load template from location `{fromUri}`", ex);
        }
    }

    private void ValidateDescriptor(TemplateInfo templateInfo, string templateLocation)
    {
        if (templateInfo.SchemaVersion != 1)
        {
            throw new TemplateLoadingException("Template descriptor is missing required SchemaVersion = 1.");
        }

        if (string.IsNullOrWhiteSpace(templateInfo.TemplateId))
        {
            throw new TemplateLoadingException("Template descriptor is missing required TemplateId.");
        }

        if (string.IsNullOrWhiteSpace(templateInfo.DisplayName) && string.IsNullOrWhiteSpace(templateInfo.Name))
        {
            throw new TemplateLoadingException("Template descriptor is missing required DisplayName.");
        }

        if (string.IsNullOrWhiteSpace(templateInfo.Description))
        {
            throw new TemplateLoadingException("Template descriptor is missing required Description.");
        }

        if (string.IsNullOrWhiteSpace(templateInfo.Icon))
        {
            throw new TemplateLoadingException("Template descriptor is missing required Icon.");
        }

        if (string.IsNullOrWhiteSpace(templateInfo.Preview))
        {
            throw new TemplateLoadingException("Template descriptor is missing required Preview.");
        }

        if (string.IsNullOrWhiteSpace(templateInfo.SourceFolder))
        {
            throw new TemplateLoadingException("Template descriptor is missing required SourceFolder.");
        }

        if (filesystem.Path.IsPathRooted(templateInfo.SourceFolder))
        {
            throw new TemplateLoadingException("Template SourceFolder must be relative to the template folder.");
        }

        var sourceFolder = filesystem.Path.GetFullPath(filesystem.Path.Combine(templateLocation, templateInfo.SourceFolder));
        if (!filesystem.Directory.Exists(sourceFolder))
        {
            throw new TemplateLoadingException($"Template SourceFolder `{templateInfo.SourceFolder}` does not exist.");
        }

        ValidateTemplateRelativePath(templateInfo.Icon, "Icon");
        ValidateTemplateRelativePath(templateInfo.Preview, "Preview");
        if (!templateInfo.PreviewImages.Contains(templateInfo.Preview, StringComparer.Ordinal))
        {
            templateInfo.PreviewImages.Insert(0, templateInfo.Preview);
        }

        if (templateInfo.StarterScene is null)
        {
            throw new TemplateLoadingException("Template descriptor is missing required StarterScene.");
        }

        if (templateInfo.AuthoringMounts.Count == 0)
        {
            throw new TemplateLoadingException("Template descriptor is missing required AuthoringMounts.");
        }

        if (!templateInfo.AuthoringMounts.Any(static mount =>
                string.Equals(mount.Name, Constants.ContentFolderName, StringComparison.OrdinalIgnoreCase)
                && string.Equals(mount.RelativePath, Constants.ContentFolderName, StringComparison.OrdinalIgnoreCase)))
        {
            throw new TemplateLoadingException("Template descriptor AuthoringMounts must include Content -> Content.");
        }
    }

    private void ValidatePayload(TemplateInfo templateInfo)
    {
        var sourceFolder = filesystem.Path.GetFullPath(filesystem.Path.Combine(templateInfo.Location, templateInfo.SourceFolder ?? "."));
        if (filesystem.Directory
            .EnumerateFiles(sourceFolder, Constants.ProjectFileName, SearchOption.AllDirectories)
            .Any())
        {
            throw new TemplateLoadingException($"Template payload must not contain `{Constants.ProjectFileName}`.");
        }

        foreach (var requiredFolder in RequiredTemplateFolders)
        {
            var path = filesystem.Path.Combine(sourceFolder, requiredFolder);
            if (!filesystem.Directory.Exists(path))
            {
                throw new TemplateLoadingException($"Template payload is missing required folder `{requiredFolder}`.");
            }
        }

        var starterScenePath = ResolveAssetRelativePath(templateInfo, templateInfo.StarterScene!.AssetUri, templateInfo.StarterScene.RelativePath, "StarterScene");
        if (!filesystem.File.Exists(filesystem.Path.Combine(sourceFolder, starterScenePath)))
        {
            throw new TemplateLoadingException($"Template payload is missing starter scene `{starterScenePath}`.");
        }

        foreach (var content in templateInfo.StarterContent)
        {
            if (string.IsNullOrWhiteSpace(content.Kind))
            {
                throw new TemplateLoadingException("Template StarterContent entry is missing required Kind.");
            }

            var starterContentPath = ResolveAssetRelativePath(templateInfo, content.AssetUri, content.RelativePath, "StarterContent");
            if (!filesystem.File.Exists(filesystem.Path.Combine(sourceFolder, starterContentPath)))
            {
                throw new TemplateLoadingException($"Template payload is missing starter content `{starterContentPath}`.");
            }
        }
    }

    private static void ValidateTemplateRelativePath(string relativePath, string fieldName)
    {
        if (Path.IsPathRooted(relativePath))
        {
            throw new TemplateLoadingException($"Template {fieldName} must be relative.");
        }

        var normalized = relativePath.Replace('\\', '/').Trim('/');
        if (normalized.Split('/', StringSplitOptions.RemoveEmptyEntries).Any(static segment => segment is "." or ".."))
        {
            throw new TemplateLoadingException($"Template {fieldName} must not contain '.' or '..' segments.");
        }
    }

    private static string ResolveAssetRelativePath(
        TemplateInfo templateInfo,
        Uri assetUri,
        string? declaredRelativePath,
        string fieldName)
    {
        if (!string.Equals(assetUri.Scheme, "asset", StringComparison.OrdinalIgnoreCase))
        {
            throw new TemplateLoadingException($"Template {fieldName} URI must use the asset scheme.");
        }

        var assetPath = Uri.UnescapeDataString(assetUri.AbsolutePath).TrimStart('/');
        var slash = assetPath.IndexOf('/', StringComparison.Ordinal);
        if (slash <= 0)
        {
            throw new TemplateLoadingException($"Template {fieldName} URI must include an authoring mount.");
        }

        var mountName = assetPath[..slash];
        var mountRelativePath = assetPath[(slash + 1)..].Replace('\\', '/').Trim('/');
        var mount = templateInfo.AuthoringMounts.FirstOrDefault(mount =>
            string.Equals(mount.Name, mountName, StringComparison.OrdinalIgnoreCase));
        if (mount is null)
        {
            throw new TemplateLoadingException($"Template {fieldName} URI targets unknown authoring mount `{mountName}`.");
        }

        var expectedPath = Path.Combine(mount.RelativePath, mountRelativePath).Replace('\\', '/').Trim('/');
        if (!string.IsNullOrWhiteSpace(declaredRelativePath))
        {
            ValidateTemplateRelativePath(declaredRelativePath, $"{fieldName}.RelativePath");
            var normalizedDeclaredPath = declaredRelativePath.Replace('\\', '/').Trim('/');
            if (!string.Equals(normalizedDeclaredPath, expectedPath, StringComparison.OrdinalIgnoreCase))
            {
                throw new TemplateLoadingException($"Template {fieldName}.RelativePath does not match AssetUri.");
            }
        }

        return expectedPath;
    }
}
