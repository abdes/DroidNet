// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.IO.Abstractions;
using DroidNet.Config;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;

namespace DroidNet.Aura.Decoration;

/// <summary>
/// Settings service responsible for persisting <see cref="WindowDecorationSettings"/> and resolving
/// effective window decorations from code-defined defaults and persisted category overrides.
/// </summary>
public sealed partial class WindowDecorationSettingsService : SettingsService<WindowDecorationSettings>, IWindowDecorationSettingsService
{
    /// <summary>
    /// Code-defined default window decoration options for standard window categories.
    /// </summary>
    /// <remarks>
    /// These defaults are hard-coded in the application and are used as fallbacks when no persisted
    /// category override exists. They are based on the preset configurations from
    /// <see cref="WindowDecorationBuilder"/>.
    /// </remarks>
    private static readonly Dictionary<WindowCategory, WindowDecorationOptions> Defaults = new()
    {
        [WindowCategory.Main] = new WindowDecorationOptions
        {
            Category = WindowCategory.Main,
            ChromeEnabled = true,
            TitleBar = TitleBarOptions.Default with { Height = 40.0 },
            Buttons = WindowButtonsOptions.Default,
            Backdrop = BackdropKind.MicaAlt,
        },
        [WindowCategory.Secondary] = new WindowDecorationOptions
        {
            Category = WindowCategory.Secondary,
            ChromeEnabled = true,
            TitleBar = TitleBarOptions.Default,
            Buttons = WindowButtonsOptions.Default,
            Backdrop = BackdropKind.None,
        },
        [WindowCategory.Tool] = new WindowDecorationOptions
        {
            Category = WindowCategory.Tool,
            ChromeEnabled = true,
            TitleBar = TitleBarOptions.Default,
            Buttons = WindowButtonsOptions.Default with { ShowMaximize = false },
            Backdrop = BackdropKind.None,
        },
        [WindowCategory.Document] = new WindowDecorationOptions
        {
            Category = WindowCategory.Document,
            ChromeEnabled = true,
            TitleBar = TitleBarOptions.Default,
            Buttons = WindowButtonsOptions.Default,
            Backdrop = BackdropKind.Mica,
        },
        [WindowCategory.System] = new WindowDecorationOptions
        {
            Category = WindowCategory.System,
            ChromeEnabled = true,
            TitleBar = TitleBarOptions.Default,
            Buttons = WindowButtonsOptions.Default,
            Backdrop = BackdropKind.None,
        },
    };

    private readonly string configFilePath;
    private Dictionary<WindowCategory, WindowDecorationOptions> categoryOverrides;

    /// <summary>
    /// Initializes a new instance of the <see cref="WindowDecorationSettingsService"/> class.
    /// </summary>
    /// <param name="settingsMonitor">Options monitor providing persisted settings.</param>
    /// <param name="fs">The abstracted file system.</param>
    /// <param name="finder">Path finder used to resolve configuration file locations.</param>
    /// <param name="loggerFactory">Optional logger factory.</param>
    public WindowDecorationSettingsService(
        IOptionsMonitor<WindowDecorationSettings> settingsMonitor,
        IFileSystem fs,
        IPathFinder finder,
        ILoggerFactory? loggerFactory = null)
        : base(settingsMonitor, fs, loggerFactory ?? NullLoggerFactory.Instance)
    {
        ArgumentNullException.ThrowIfNull(finder);

        var current = settingsMonitor.CurrentValue ?? new WindowDecorationSettings();
        this.categoryOverrides = CloneCategoryOverrides(current.CategoryOverrides);
        this.configFilePath = finder.GetConfigFilePath(WindowDecorationSettings.ConfigFileName);
    }

    /// <summary>
    /// Gets the current read-only dictionary of category-specific window decoration overrides.
    /// </summary>
    public IReadOnlyDictionary<WindowCategory, WindowDecorationOptions> CategoryOverrides => this.categoryOverrides;

    /// <inheritdoc/>
    public WindowDecorationOptions GetEffectiveDecoration(WindowCategory category)
    {
        // First, check for a persisted category override
        if (this.categoryOverrides.TryGetValue(category, out var overrideOptions))
        {
            return overrideOptions;
        }

        // Fall back to code-defined default for the category
        if (Defaults.TryGetValue(category, out var defaultOptions))
        {
            return defaultOptions;
        }

        // Final fallback: return the "Unknown" category default
        return Defaults[WindowCategory.System];
    }

    /// <inheritdoc/>
    public void SetCategoryOverride(WindowCategory category, WindowDecorationOptions options)
    {
        ArgumentNullException.ThrowIfNull(options);

        var normalized = EnsureCategory(options, category);
        normalized.Validate();

        var updated = new Dictionary<WindowCategory, WindowDecorationOptions>(
            this.categoryOverrides,
            this.categoryOverrides.Comparer)
        {
            [category] = normalized,
        };

        _ = this.SetField(ref this.categoryOverrides, updated, nameof(this.CategoryOverrides));
    }

    /// <inheritdoc/>
    public bool RemoveCategoryOverride(WindowCategory category)
    {
        if (!this.categoryOverrides.ContainsKey(category))
        {
            return false;
        }

        var updated = new Dictionary<WindowCategory, WindowDecorationOptions>(
            this.categoryOverrides,
            this.categoryOverrides.Comparer);
        var removed = updated.Remove(category);

        if (!removed)
        {
            return false;
        }

        _ = this.SetField(ref this.categoryOverrides, updated, nameof(this.CategoryOverrides));
        return true;
    }

    /// <summary>
    /// Saves the current decoration settings asynchronously.
    /// </summary>
    /// <param name="cancellationToken">Cancellation token for the save operation.</param>
    /// <returns><see langword="true"/> when the settings were persisted successfully.</returns>
    public ValueTask<bool> SaveAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(this.SaveSettings());
    }

    /// <inheritdoc/>
    protected override WindowDecorationSettings GetSettingsSnapshot()
    {
        var snapshot = new WindowDecorationSettings();

        foreach (var pair in this.categoryOverrides)
        {
            snapshot.CategoryOverrides[pair.Key] = pair.Value;
        }

        return snapshot;
    }

    /// <inheritdoc/>
    protected override void UpdateProperties(WindowDecorationSettings newSettings)
    {
        ArgumentNullException.ThrowIfNull(newSettings);

        var overrides = CloneCategoryOverrides(newSettings.CategoryOverrides);

        _ = this.SetField(ref this.categoryOverrides, overrides, nameof(this.CategoryOverrides));
    }

    /// <inheritdoc/>
    protected override string GetConfigFilePath() => this.configFilePath;

    /// <inheritdoc/>
    protected override string GetConfigSectionName() => WindowDecorationSettings.ConfigSectionName;

    private static Dictionary<WindowCategory, WindowDecorationOptions> CloneCategoryOverrides(
        IDictionary<WindowCategory, WindowDecorationOptions>? source)
    {
        var clone = new Dictionary<WindowCategory, WindowDecorationOptions>();

        if (source is null)
        {
            return clone;
        }

        foreach (var pair in source)
        {
            var options = pair.Value ?? throw new ValidationException(
                $"Category override for '{pair.Key}' cannot be null.");

            var normalized = EnsureCategory(options, pair.Key);
            normalized.Validate();

            clone[pair.Key] = normalized;
        }

        return clone;
    }

    private static WindowDecorationOptions EnsureCategory(WindowDecorationOptions options, WindowCategory category)
        => options.Category.Equals(category)
            ? options
            : options with { Category = category };
}
