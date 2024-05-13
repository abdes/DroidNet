// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Services;

using System.Diagnostics;
using System.IO.Abstractions;
using System.Reactive.Linq;
using System.Reflection;
using CommunityToolkit.Mvvm.DependencyInjection;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Oxygen.Editor.Core.Services;
using Oxygen.Editor.Data;
using Oxygen.Editor.Data.Models;
using Oxygen.Editor.ProjectBrowser.Config;
using Oxygen.Editor.ProjectBrowser.Templates;

/// <summary>Provides method to access and manipulate templates.</summary>
public partial class TemplatesService : ITemplatesService
{
    private readonly ILogger<TemplatesService> logger;
    private readonly IFileSystem fs;
    private readonly ProjectBrowserSettings settings;
    private readonly ITemplatesSource templatesSource;

    public TemplatesService(
        ILogger<TemplatesService> logger,
        IFileSystem fs,
        IPathFinder pathFinder,
        IOptions<ProjectBrowserSettings> settings,
        ITemplatesSource templatesSource)
    {
        this.logger = logger;
        this.fs = fs;
        this.settings = settings.Value;

        // This is the non-keyed implementation of ITemplateSource registered with the DI container.
        this.templatesSource = templatesSource;

        this.BuiltinTemplates = fs.Path.Combine(
            pathFinder.ProgramData,
            $"{Assembly.GetAssembly(typeof(ProjectBrowserSettings))!.GetName().Name}",
            "Assets",
            "Templates");

        this.LocalTemplates = fs.Path.Combine(pathFinder.LocalAppData, "Templates");
    }

    private string BuiltinTemplates { get; }

    private string LocalTemplates { get; }

    public static async Task TryClearRecentUsageAsync(RecentlyUsedTemplate template)
    {
        try
        {
            await ClearRecentUsageAsync(template).ConfigureAwait(true);
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Failed to remove entry from the most recently used templates: {ex.Message}");
        }
    }

    public static async Task ClearRecentUsageAsync(RecentlyUsedTemplate template)
    {
        var state = Ioc.Default.CreateAsyncScope()
            .ServiceProvider.GetRequiredService<PersistentState>();
        await using (state.ConfigureAwait(true))
        {
            _ = state.RecentlyUsedTemplates!.Remove(template);
            _ = await state.SaveChangesAsync().ConfigureAwait(true);
        }
    }

    public IObservable<ITemplateInfo> GetLocalTemplates()
        => Observable.Create<ITemplateInfo>(
            async (observer) =>
            {
                // Load builtin templates
                foreach (var template in this.settings.BuiltinTemplates)
                {
                    var templateFullPath = this.fs.Path.Combine(this.BuiltinTemplates, template);
                    try
                    {
                        var templateUri = new Uri($"{Uri.UriSchemeFile}:///{templateFullPath}");
                        var templateInfo
                            = await this.templatesSource.LoadTemplateAsync(templateUri).ConfigureAwait(true);

                        // Update template last used time
                        templateInfo.LastUsedOn = LoadUsageData()
                            .TryGetValue(templateInfo.Location!, out var recentlyUsedTemplate)
                            ? recentlyUsedTemplate.LastUsedOn
                            : DateTime.MaxValue;

                        observer.OnNext(templateInfo);
                    }
                    catch (Exception ex)
                    {
                        this.CouldNotLoadTemplate(templateFullPath, ex.Message);
                    }
                }
            });

    public bool HasRecentlyUsedTemplates()
    {
        using var state = Ioc.Default.CreateScope()
            .ServiceProvider.GetRequiredService<PersistentState>();
        return state.RecentlyUsedTemplates?.Any() == true;
    }

    public IObservable<ITemplateInfo> GetRecentlyUsedTemplates()
        => Observable.Create<ITemplateInfo>(
            async (observer) =>
            {
                // Load builtin templates
                foreach (var template in LoadUsageData()
                             .OrderByDescending(item => item.Value.LastUsedOn))
                {
                    try
                    {
                        var templateDescriptor = Path.Combine(template.Key, "Template.json");
                        var templateInfo = await this.templatesSource.LoadTemplateAsync(new Uri(templateDescriptor))
                            .ConfigureAwait(true);

                        templateInfo.LastUsedOn = template.Value.LastUsedOn;
                        observer.OnNext(templateInfo);
                    }
                    catch (Exception ex)
                    {
                        this.CouldNotLoadTemplate(template.Key, ex.Message);
                        await TryClearRecentUsageAsync(template.Value).ConfigureAwait(true);
                    }
                }
            });

    private static Dictionary<string, RecentlyUsedTemplate> LoadUsageData()
    {
        using var state = Ioc.Default.CreateScope()
            .ServiceProvider.GetRequiredService<PersistentState>();
        if (state.RecentlyUsedProjects?.Any() == true)
        {
            return state.RecentlyUsedTemplates!.ToDictionary(t => t.Location!, StringComparer.Ordinal);
        }

        return new Dictionary<string, RecentlyUsedTemplate>(StringComparer.Ordinal);
    }

    [LoggerMessage(
        EventId = 1000,
        Level = LogLevel.Error,
        Message = "Could not load template at `{templatePath}`; {error}")]
    partial void CouldNotLoadTemplate(string templatePath, string error);

    [LoggerMessage(
        EventId = 1001,
        Level = LogLevel.Error,
        Message = "Could not load templates MRU at `{mruPath}`; {error}")]
    partial void CouldNotLoadTemplatesMru(string mruPath, string error);
}
