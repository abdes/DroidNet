// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Settings;
using DroidNet.Config;
using Microsoft.Extensions.Logging;

namespace DroidNet.Aura.Decoration;

/// <summary>
/// Settings service responsible for persisting <see cref="WindowDecorationSettings"/> and resolving
/// effective window decorations from code-defined defaults and persisted category overrides.
/// </summary>
public sealed partial class WindowDecorationSettingsService(SettingsManager manager, ILoggerFactory? factory = null)
    : SettingsService<IWindowDecorationSettings>(manager, factory), IWindowDecorationSettings
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
            TitleBar = TitleBarOptions.Default,
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
            TitleBar = null,
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
            ChromeEnabled = false,
            TitleBar = null,
            Buttons = WindowButtonsOptions.Default,
            Backdrop = BackdropKind.None,
        },
    };

    private Dictionary<WindowCategory, WindowDecorationOptions> categoryOverrides = [];

    /// <summary>
    /// Gets the current read-only dictionary of category-specific window decoration overrides.
    /// </summary>
    /// <inheritdoc/>
    public IReadOnlyDictionary<WindowCategory, WindowDecorationOptions> CategoryOverrides => this.categoryOverrides;

    /// <inheritdoc/>
    public override string SectionName => WindowDecorationSettings.ConfigSectionName;

    /// <inheritdoc/>
    public override Type SettingsType => typeof(WindowDecorationSettings);

    /// <inheritdoc/>
    public override SettingsSectionMetadata SectionMetadata { get; set; } = new()
    {
        SchemaVersion = "20251022",
        Service = typeof(WindowDecorationSettingsService).FullName,
    };

    /// <summary>
    /// Resolves the effective decoration options for a given window category.
    /// </summary>
    /// <param name="category">The window category.</param>
    /// <returns>The effective decoration options combining code-defined defaults with any persisted overrides.</returns>
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

    /// <summary>
    /// Sets a persisted override for a window category, replacing any existing override.
    /// </summary>
    /// <param name="category">The window category to override.</param>
    /// <param name="options">The decoration options to persist as an override.</param>
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

    /// <summary>
    /// Removes a persisted category override, reverting to the code-defined default.
    /// </summary>
    /// <param name="category">The window category to revert to default.</param>
    /// <returns><see langword="true"/> if an override was removed; <see langword="false"/> otherwise.</returns>
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

    /// <inheritdoc/>
    protected override object GetSettingsSnapshot()
    {
        var snapshot = new WindowDecorationSettings
        {
            CategoryOverrides = new Dictionary<WindowCategory, WindowDecorationOptions>(this.categoryOverrides),
        };

        return snapshot;
    }

    /// <inheritdoc/>
    protected override void UpdateProperties(IWindowDecorationSettings newSettings)
    {
        ArgumentNullException.ThrowIfNull(newSettings);

        var overrides = CloneCategoryOverrides(newSettings.CategoryOverrides);

        _ = this.SetField(ref this.categoryOverrides, overrides, nameof(this.CategoryOverrides));
    }

    /// <inheritdoc/>
    protected override WindowDecorationSettings CreateDefaultSettings() => new();

    private static Dictionary<WindowCategory, WindowDecorationOptions> CloneCategoryOverrides(
        IEnumerable<KeyValuePair<WindowCategory, WindowDecorationOptions>>? source)
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
