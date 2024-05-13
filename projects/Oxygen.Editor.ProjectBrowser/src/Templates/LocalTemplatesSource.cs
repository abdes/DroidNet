// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Templates;

using System.Diagnostics;
using System.IO.Abstractions;
using System.Reflection;
using System.Text.Json;
using Microsoft.Extensions.Options;
using Oxygen.Editor.Core.Services;
using Oxygen.Editor.ProjectBrowser.Config;
using Oxygen.Editor.ProjectBrowser.Utils;

public class LocalTemplatesSource
{
    private readonly IFileSystem fs;
    private readonly ProjectBrowserSettings settings;
    private readonly JsonSerializerOptions jsonSerializerOptions;

    public LocalTemplatesSource(
        IFileSystem filesystem,
        IOptions<ProjectBrowserSettings> settings,
        IPathFinder pathFinder)
    {
        this.fs = filesystem;
        this.settings = settings.Value;

        this.BuiltinTemplates = Path.Combine(
            pathFinder.ProgramData,
            $"{Assembly.GetAssembly(typeof(ProjectBrowserSettings))!.GetName().Name}",
            "Assets",
            "Templates");
        this.LocalTemplates = Path.Combine(pathFinder.LocalAppData, "Templates");

        this.jsonSerializerOptions = new JsonSerializerOptions
        {
            AllowTrailingCommas = true,
            Converters = { new CategoryJsonConverter(this.settings) },
        };
    }

    private string BuiltinTemplates { get; }

    private string LocalTemplates { get; }

    public async Task<ITemplateInfo> LoadTemplateAsync(string path)
    {
        var fullPath = Path.Combine(this.IsBuiltin(path) ? this.BuiltinTemplates : this.LocalTemplates, path);

        if (!this.fs.File.Exists(fullPath))
        {
            Debug.WriteLine($"Template info file `{fullPath}` does not exist");
            throw new ArgumentException($"Template info file `{fullPath}` does not exist", nameof(path));
        }

        Debug.WriteLine($"Loading project template info from `{fullPath}`");

        try
        {
            var json = await this.fs.File.ReadAllTextAsync(fullPath, CancellationToken.None).ConfigureAwait(true);

            var template = JsonSerializer.Deserialize<Template>(json, this.jsonSerializerOptions);
            Debug.Assert(template != null, $"Json content of template at `{fullPath}` is not valid");

            // Update the paths for the icon and preview images to become absolute
            template.Location = Path.GetDirectoryName(fullPath);
            if (template.Icon != null)
            {
                template.Icon = Path.Combine(template.Location!, template.Icon);
            }

            for (var index = 0; index < template.PreviewImages.Count; index++)
            {
                template.PreviewImages[index] = Path.Combine(template.Location!, template.PreviewImages[index]);
            }

            return template;
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Failed to load template info: {ex.Message}");
            throw;
        }
    }

    private bool IsBuiltin(string path)
        => this.settings.BuiltinTemplates.Contains(path, StringComparer.Ordinal);
}
