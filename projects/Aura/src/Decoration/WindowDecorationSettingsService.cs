// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;
using System.IO.Abstractions;
using System.Threading;
using System.Threading.Tasks;
using DroidNet.Config;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;

namespace DroidNet.Aura.Decoration;

/// <summary>
/// Settings service responsible for persisting <see cref="WindowDecorationSettings"/>.
/// </summary>
public sealed class WindowDecorationSettingsService : SettingsService<WindowDecorationSettings>, IWindowDecorationSettings
{
    private readonly string configFilePath;
    private Dictionary<string, WindowDecorationOptions> defaultsByCategory;
    private Dictionary<string, WindowDecorationOptions> overridesByType;

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
        this.defaultsByCategory = CloneDefaults(current.DefaultsByCategory);
        this.overridesByType = CloneOverrides(current.OverridesByType);
        this.configFilePath = finder.GetConfigFilePath(WindowDecorationSettings.ConfigFileName);
    }

    /// <inheritdoc/>
    public IReadOnlyDictionary<string, WindowDecorationOptions> DefaultsByCategory => this.defaultsByCategory;

    /// <inheritdoc/>
    public IReadOnlyDictionary<string, WindowDecorationOptions> OverridesByType => this.overridesByType;

    /// <inheritdoc/>
    public WindowDecorationOptions? GetDefaultForCategory(string category)
    {
        var key = NormalizeCategoryKey(category);
        return this.defaultsByCategory.TryGetValue(key, out var options) ? options : null;
    }

    /// <inheritdoc/>
    public WindowDecorationOptions? GetOverrideForType(string windowType)
    {
        var key = NormalizeWindowTypeKey(windowType);
        return this.overridesByType.TryGetValue(key, out var options) ? options : null;
    }

    /// <inheritdoc/>
    public void SetDefaultForCategory(string category, WindowDecorationOptions options)
    {
        var key = NormalizeCategoryKey(category);
        ArgumentNullException.ThrowIfNull(options);

        var normalized = EnsureCategory(options, key);
        normalized.Validate();

        var updated = new Dictionary<string, WindowDecorationOptions>(this.defaultsByCategory, this.defaultsByCategory.Comparer);
        updated[key] = normalized;

        _ = this.SetField(ref this.defaultsByCategory, updated, nameof(this.DefaultsByCategory));
    }

    /// <inheritdoc/>
    public bool RemoveDefaultForCategory(string category)
    {
        var key = NormalizeCategoryKey(category);

        if (!this.defaultsByCategory.ContainsKey(key))
        {
            return false;
        }

        var updated = new Dictionary<string, WindowDecorationOptions>(this.defaultsByCategory, this.defaultsByCategory.Comparer);
        var removed = updated.Remove(key);

        if (!removed)
        {
            return false;
        }

        _ = this.SetField(ref this.defaultsByCategory, updated, nameof(this.DefaultsByCategory));
        return true;
    }

    /// <inheritdoc/>
    public void SetOverrideForType(string windowType, WindowDecorationOptions options)
    {
        var key = NormalizeWindowTypeKey(windowType);
        ArgumentNullException.ThrowIfNull(options);

        options.Validate();

        var updated = new Dictionary<string, WindowDecorationOptions>(this.overridesByType, this.overridesByType.Comparer);
        updated[key] = options;

        _ = this.SetField(ref this.overridesByType, updated, nameof(this.OverridesByType));
    }

    /// <inheritdoc/>
    public bool RemoveOverrideForType(string windowType)
    {
        var key = NormalizeWindowTypeKey(windowType);

        if (!this.overridesByType.ContainsKey(key))
        {
            return false;
        }

        var updated = new Dictionary<string, WindowDecorationOptions>(this.overridesByType, this.overridesByType.Comparer);
        var removed = updated.Remove(key);

        if (!removed)
        {
            return false;
        }

        _ = this.SetField(ref this.overridesByType, updated, nameof(this.OverridesByType));
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

        foreach (var pair in this.defaultsByCategory)
        {
            snapshot.DefaultsByCategory[pair.Key] = pair.Value;
        }

        foreach (var pair in this.overridesByType)
        {
            snapshot.OverridesByType[pair.Key] = pair.Value;
        }

        return snapshot;
    }

    /// <inheritdoc/>
    protected override void UpdateProperties(WindowDecorationSettings newSettings)
    {
        ArgumentNullException.ThrowIfNull(newSettings);

        var defaults = CloneDefaults(newSettings.DefaultsByCategory);
        var overrides = CloneOverrides(newSettings.OverridesByType);

        _ = this.SetField(ref this.defaultsByCategory, defaults, nameof(this.DefaultsByCategory));
        _ = this.SetField(ref this.overridesByType, overrides, nameof(this.OverridesByType));
    }

    /// <inheritdoc/>
    protected override string GetConfigFilePath() => this.configFilePath;

    /// <inheritdoc/>
    protected override string GetConfigSectionName() => WindowDecorationSettings.ConfigSectionName;

    private static Dictionary<string, WindowDecorationOptions> CloneDefaults(
        IDictionary<string, WindowDecorationOptions>? source)
    {
        var clone = new Dictionary<string, WindowDecorationOptions>(StringComparer.OrdinalIgnoreCase);

        if (source is null)
        {
            return clone;
        }

        foreach (var pair in source)
        {
            var key = NormalizeCategoryKey(pair.Key);
            var options = pair.Value ?? throw new ValidationException(
                $"Default window decoration for category '{pair.Key}' cannot be null.");

            var normalized = EnsureCategory(options, key);
            normalized.Validate();

            clone[key] = normalized;
        }

        return clone;
    }

    private static Dictionary<string, WindowDecorationOptions> CloneOverrides(
        IDictionary<string, WindowDecorationOptions>? source)
    {
        var clone = new Dictionary<string, WindowDecorationOptions>(StringComparer.Ordinal);

        if (source is null)
        {
            return clone;
        }

        foreach (var pair in source)
        {
            var key = NormalizeWindowTypeKey(pair.Key);
            var options = pair.Value ?? throw new ValidationException(
                $"Window decoration override for '{pair.Key}' cannot be null.");

            options.Validate();
            clone[key] = options;
        }

        return clone;
    }

    private static string NormalizeCategoryKey(string category)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(category);
        return category.Trim();
    }

    private static string NormalizeWindowTypeKey(string windowType)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(windowType);
        return windowType.Trim();
    }

    private static WindowDecorationOptions EnsureCategory(WindowDecorationOptions options, string category)
        => string.Equals(options.Category, category, StringComparison.OrdinalIgnoreCase)
            ? options
            : options with { Category = category };
}
