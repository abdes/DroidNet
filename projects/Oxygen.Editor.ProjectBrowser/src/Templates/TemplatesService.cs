// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.IO.Abstractions;
using System.Reactive.Linq;
using System.Reflection;
using DroidNet.Config;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Data.Services;

namespace Oxygen.Editor.ProjectBrowser.Templates;

/// <summary>
/// Provides services for managing and accessing project templates in the Oxygen Editor.
/// </summary>
/// <remarks>
/// <para>
/// TemplatesService manages both built-in and custom project templates, providing functionality for:
/// - Loading and validation of templates from the filesystem
/// - Tracking template usage history
/// - Managing recently used templates list
/// - Logging template operations.
/// </para>
/// <para>
/// The service uses abstracted filesystem operations through <see cref="IFileSystem"/> and
/// supports detailed operation logging when a logger factory is provided.
/// </para>
/// <para>
/// Built-in templates are stored in predefined locations under the program data directory,
/// organized by categories like Games and Visualization.
/// </para>
/// </remarks>
/// <param name="fs">Abstracted filesystem operations provider.</param>
/// <param name="pathFinder">Service for resolving system and application paths.</param>
/// <param name="templatesSource">Provider for loading template information.</param>
/// <param name="loggerFactory">Optional factory for creating operation loggers.</param>
/// <example>
/// <para><strong>Example Usage</strong></para>
/// <code><![CDATA[
/// // Create the service with dependencies
/// var templatesService = new TemplatesService(
///     new PersistentState(),
///     new FileSystem(),
///     new PathFinder(),
///     new LocalTemplatesSource(new FileSystem()),
///     LoggerFactory.Create(builder => builder.AddConsole())
/// );
///
/// // Enumerate available templates
/// await foreach (var template in templatesService.GetLocalTemplatesAsync())
/// {
///     Console.WriteLine($"Found template: {template.Name}");
///     Console.WriteLine($"Category: {template.Category}");
///     Console.WriteLine($"Last used: {template.LastUsedOn}");
/// }
///
/// // Access recently used templates
/// if (templatesService.HasRecentlyUsedTemplates())
/// {
///     templatesService.GetRecentlyUsedTemplates()
///         .Subscribe(template =>
///         {
///             Console.WriteLine($"Recent template: {template.Name}");
///         });
/// }
/// ]]></code>
/// </example>
public partial class TemplatesService(
    ITemplateUsageService templateUsage,
    IFileSystem fs,
    IPathFinder pathFinder,
    ITemplatesSource templatesSource,
    ILoggerFactory? loggerFactory = null) : ITemplatesService
{
    private static readonly List<string> Templates =
    [
        "Games/Blank/Template.json",
        "Games/First Person/Template.json",
        "Visualization/Blank/Template.json",
    ];

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Performance", "CA1823:Avoid unused private fields", Justification = "used by generated logging methods")]
    private readonly ILogger logger = loggerFactory?.CreateLogger<TemplatesService>() ?? NullLoggerFactory.Instance.CreateLogger<TemplatesService>();

    private List<string> BuiltinTemplates { get; } = Templates.ConvertAll(
                location => fs.Path.Combine(
                    pathFinder.ProgramData,
                    $"{Assembly.GetAssembly(typeof(ProjectBrowserSettings))!.GetName().Name}",
                    "Assets",
                    "Templates",
                    location));

    /// <inheritdoc/>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "all exceptions are logged, but we gracefully continue with other items")]
    public async IAsyncEnumerable<ITemplateInfo> GetLocalTemplatesAsync()
    {
        var tasks = this.BuiltinTemplates.Select(async template =>
        {
            var templateUri = new Uri($"{Uri.UriSchemeFile}:///{template}");

            ITemplateInfo? templateInfo;
            try
            {
                templateInfo = await templatesSource.LoadTemplateAsync(templateUri).ConfigureAwait(false);
            }
            catch (Exception ex)
            {
                // Log the error, but continue with the rest of templates
                this.CouldNotLoadTemplate(template, ex.Message);
                return null;
            }

            return templateInfo;
        });

        var results = await Task.WhenAll(tasks).ConfigureAwait(false);

        foreach (var templateInfo in results)
        {
            if (templateInfo != null)
            {
                // Update template last used time
                templateInfo.LastUsedOn = (await templateUsage.GetTemplateUsageAsync(templateInfo.Location).ConfigureAwait(false))?.LastUsedOn ?? DateTime.MaxValue;

                yield return templateInfo;
            }
        }
    }

    /// <inheritdoc/>
    public Task<bool> HasRecentlyUsedTemplatesAsync() => templateUsage.HasRecentlyUsedTemplatesAsync();

    /// <inheritdoc/>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "no exception is worth reporting, just remove the erroneous template usage data")]
    public IObservable<ITemplateInfo> GetRecentlyUsedTemplates()
        => Observable.Create<ITemplateInfo>(
            async (observer) =>
            {
                // Load builtin templates
                foreach (var template in await templateUsage.GetMostRecentlyUsedTemplatesAsync().ConfigureAwait(false))
                {
                    try
                    {
                        var templateDescriptor = fs.Path.Combine(template.Location, "Template.json");
                        var templateInfo = await templatesSource.LoadTemplateAsync(new Uri(templateDescriptor)).ConfigureAwait(true);

                        templateInfo.LastUsedOn = template.LastUsedOn;
                        observer.OnNext(templateInfo);
                    }
                    catch (Exception ex)
                    {
                        this.CouldNotLoadTemplate(template.Location, ex.Message);
                        await templateUsage.DeleteTemplateUsageAsync(template.Location).ConfigureAwait(true);
                    }
                }
            });

    [LoggerMessage(
        EventId = 1000,
        Level = LogLevel.Error,
        Message = "Could not load template at `{templatePath}`; {error}")]
    private partial void CouldNotLoadTemplate(string templatePath, string error);

    [LoggerMessage(
        EventId = 1001,
        Level = LogLevel.Error,
        Message = "Could not load templates MRU at `{mruPath}`; {error}")]
    private partial void CouldNotLoadTemplatesMru(string mruPath, string error);
}
