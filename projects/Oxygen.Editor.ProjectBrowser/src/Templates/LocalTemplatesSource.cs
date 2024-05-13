// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Templates;

using System.Diagnostics;
using System.IO.Abstractions;
using System.Text.Json;
using Microsoft.Extensions.Options;
using Oxygen.Editor.ProjectBrowser.Config;
using Oxygen.Editor.ProjectBrowser.Utils;

public class LocalTemplatesSource : ITemplatesSource
{
    private readonly IFileSystem fs;
    private readonly ProjectBrowserSettings settings;
    private readonly JsonSerializerOptions jsonSerializerOptions;

    public LocalTemplatesSource(
        IFileSystem filesystem,
        IOptions<ProjectBrowserSettings> settings)
    {
        this.fs = filesystem;
        this.settings = settings.Value;

        this.jsonSerializerOptions = new JsonSerializerOptions
        {
            AllowTrailingCommas = true,
            Converters = { new CategoryJsonConverter(this.settings) },
        };
    }

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
        if (!this.fs.File.Exists(fromUri.LocalPath))
        {
            throw new TemplateLoadingException(
                $"Location `{templatePath}` does not correspond to an existing template descriptor file.");
        }

        Debug.WriteLine($"Loading local project template info from `{templatePath}`");

        try
        {
            var json = await this.fs.File.ReadAllTextAsync(templatePath, CancellationToken.None).ConfigureAwait(true);

            var template = JsonSerializer.Deserialize<Template>(json, this.jsonSerializerOptions);
            Debug.Assert(template != null, $"Json content of template at `{templatePath}` is not valid");

            // Update the paths for the icon and preview images to become absolute
            template.Location = this.fs.Path.GetFullPath(this.fs.Path.GetDirectoryName(templatePath)!);
            if (template.Icon != null)
            {
                var iconPath = this.fs.Path.Combine(template.Location!, template.Icon);
                if (!this.fs.File.Exists(iconPath))
                {
                    throw new TemplateLoadingException(
                        $"Missing icon file `{template.Icon}` at the template location.");
                }

                template.Icon = iconPath;
            }

            for (var index = 0; index < template.PreviewImages.Count; index++)
            {
                var previewImagePath = this.fs.Path.Combine(template.Location!, template.PreviewImages[index]);
                if (!this.fs.File.Exists(previewImagePath))
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
