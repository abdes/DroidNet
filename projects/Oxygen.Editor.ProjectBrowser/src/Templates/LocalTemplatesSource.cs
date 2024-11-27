// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.IO.Abstractions;
using System.Text.Json;
using Oxygen.Editor.Projects;

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
                .ConfigureAwait(false);

            var templateInfo = JsonSerializer.Deserialize<TemplateInfo>(json, this.jsonSerializerOptions);
            Debug.Assert(templateInfo != null, $"Json content of template at `{templatePath}` is not valid");

            // Update the paths for the icon and preview images to become absolute
            templateInfo.Location = filesystem.Path.GetFullPath(filesystem.Path.GetDirectoryName(templatePath)!);
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

            return templateInfo;
        }
        catch (Exception ex)
        {
            throw new TemplateLoadingException($"Could not load template from location `{fromUri}`", ex);
        }
    }
}
