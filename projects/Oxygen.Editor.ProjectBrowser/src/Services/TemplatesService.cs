// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Services;

using System.Diagnostics;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.DependencyInjection;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Oxygen.Editor.Data;
using Oxygen.Editor.Data.Models;
using Oxygen.Editor.ProjectBrowser.Config;
using Oxygen.Editor.ProjectBrowser.Templates;

/// <summary>Provides method to access and manipulate templates.</summary>
public partial class TemplatesService : ITemplatesService
{
    private readonly ILogger<TemplatesService> logger;
    private readonly ProjectBrowserSettings settings;
    private readonly ITemplatesSource templatesSource;

    public TemplatesService(
        ILogger<TemplatesService> logger,
        IOptions<ProjectBrowserSettings> settings,
        ITemplatesSource templatesSource)
    {
        this.logger = logger;
        this.settings = settings.Value;
        this.templatesSource = templatesSource;
    }

    public static async Task TryClearRecentUsageAsync(RecentlyUsedTemplate template)
    {
        try
        {
            await ClearRecentUsageAsync(template);
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Failed to remove entry from the most recently used templates: {ex.Message}");
        }
    }

    public static async Task ClearRecentUsageAsync(RecentlyUsedTemplate template)
    {
        await using var state = Ioc.Default.CreateScope()
            .ServiceProvider.GetRequiredService<PersistentState>();
        _ = state.RecentlyUsedTemplates!.Remove(template);
        _ = await state.SaveChangesAsync();
    }

    public IList<ProjectCategory> GetProjectCategories()
        => this.settings.Categories;

    public ProjectCategory? GetProjectCategoryById(string id)
        => this.settings.GetProjectCategoryById(id);

    public IObservable<ITemplateInfo> GetLocalTemplates()
        => Observable.Create<ITemplateInfo>(
            async (observer) =>
            {
                // Load builtin templates
                foreach (var template in this.settings.BuiltinTemplates)
                {
                    try
                    {
                        var templateInfo = await this.templatesSource.LoadLocalTemplateAsync(template);

                        // Update template last used time
                        templateInfo.LastUsedOn = LoadUsageData()
                            .TryGetValue(templateInfo.Location!, out var recentlyUsedTemplate)
                            ? recentlyUsedTemplate.LastUsedOn
                            : DateTime.MaxValue;

                        observer.OnNext(templateInfo);
                    }
                    catch (Exception ex)
                    {
                        this.CouldNotLoadTemplate(template, ex.Message);
                    }
                }
            });

    public bool HasRecentlyUsedTemplates()
    {
        using var state = Ioc.Default.CreateScope()
            .ServiceProvider.GetRequiredService<PersistentState>();
        return state.RecentlyUsedTemplates != null && state.RecentlyUsedTemplates.Any();
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
                        var templateInfo = await this.templatesSource.LoadLocalTemplateAsync(templateDescriptor);

                        templateInfo.LastUsedOn = template.Value.LastUsedOn;
                        observer.OnNext(templateInfo);
                    }
                    catch (Exception ex)
                    {
                        this.CouldNotLoadTemplate(template.Key, ex.Message);
                        await TryClearRecentUsageAsync(template.Value);
                    }
                }
            });

    private static IDictionary<string, RecentlyUsedTemplate> LoadUsageData()
    {
        using var state = Ioc.Default.CreateScope()
            .ServiceProvider.GetRequiredService<PersistentState>();
        if (state.RecentlyUsedProjects != null && state.RecentlyUsedProjects.Any())
        {
            return state.RecentlyUsedTemplates!.ToDictionary(t => t.Location!);
        }

        return new Dictionary<string, RecentlyUsedTemplate>();
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
