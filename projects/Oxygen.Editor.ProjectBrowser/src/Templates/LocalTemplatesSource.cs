// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Templates;

using System.Diagnostics;
using System.IO.Abstractions;
using System.Text.Json;
using Oxygen.Editor.Projects;

public class LocalTemplatesSource(
    IFileSystem filesystem,
    IProjectManagerService projectManager)
    : ITemplatesSource
{
    private readonly JsonSerializerOptions jsonSerializerOptions = new()
    {
        AllowTrailingCommas = true,
        Converters = { projectManager.CategoryJsonConverter },
    };

    /// <inheritdoc />
    /// <remarks>
    /// <see cref="LocalTemplatesSource" /> only supports locations with a URi scheme <see cref="Uri.UriSchemeFile" />.
    /// </remarks>
    public bool CanLoad(Uri fromUri) => string.Equals(
        fromUri.Scheme,
        Uri.UriSchemeFile,
        StringComparison.OrdinalIgnoreCase);

    /// <inheritdoc />
    /// <remarks>
    /// Fails if the provided <paramref name="fromUri">location</paramref> does not correspond to an existing and accessible
    /// template descriptor file, if the file contains invalid data, or if any other required template file is missing at the
    /// location (such as the specified thumbnail or preview images).
    /// </remarks>
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

            var template = JsonSerializer.Deserialize<Template>(json, this.jsonSerializerOptions);
            Debug.Assert(template != null, $"Json content of template at `{templatePath}` is not valid");

            // Update the paths for the icon and preview images to become absolute
            template.Location = filesystem.Path.GetFullPath(filesystem.Path.GetDirectoryName(templatePath)!);
            if (template.Icon != null)
            {
                var iconPath = filesystem.Path.Combine(template.Location!, template.Icon);
                if (!filesystem.File.Exists(iconPath))
                {
                    throw new TemplateLoadingException(
                        $"Missing icon file `{template.Icon}` at the template location.");
                }

                template.Icon = iconPath;
            }

            for (var index = 0; index < template.PreviewImages.Count; index++)
            {
                var previewImagePath = filesystem.Path.Combine(template.Location!, template.PreviewImages[index]);
                if (!filesystem.File.Exists(previewImagePath))
                {
                    throw new TemplateLoadingException(
                        $"Missing preview image file `{previewImagePath}` at the template location.");
                }

                template.PreviewImages[index] = previewImagePath;
            }

            return template;
        }
        catch (Exception ex)
        {
            throw new TemplateLoadingException($"Could not load template from location `{fromUri}`", ex);
        }
    }
}
