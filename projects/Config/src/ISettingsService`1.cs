// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;

namespace DroidNet.Config;

/// <summary>
///     Provides an interface for managing settings with property change notification and disposable
///     functionality.
/// </summary>
/// <typeparam name="TSettings">The type of the settings interface.</typeparam>
/// <remarks>
///     Implementations of this interface should also implement <typeparamref name="TSettings"/> directly,
///     providing both service functionality (IsDirty, SaveSettings) and direct property access.
///     <para>
///     This allows consumers to cast the service to access settings properties:
///     <code>
///       ISettingsService&lt;IAppearanceSettings&gt; service = ...;
///       var settings = (IAppearanceSettings)service;
///       var theme = settings.AppThemeMode;
///     </code>
///     </para>
/// </remarks>
public interface ISettingsService<out TSettings> : INotifyPropertyChanged, IDisposable
    where TSettings : class
{
    /// <summary>
    ///     Gets a value indicating whether the settings have been modified.
    /// </summary>
    public bool IsDirty { get; }

    /// <summary>
    ///     Gets the settings instance, allowing typed access to settings properties.
    /// </summary>
    /// <remarks>
    ///     This property allows direct access to settings properties without explicit casting:
    ///     <code>
    ///       ISettingsService&lt;IAppearanceSettings&gt; service = ...;
    ///       var theme = service.Settings.AppThemeMode;
    ///     </code>
    /// </remarks>
    public TSettings Settings { get; }

    /// <summary>
    ///     Saves the current settings if they have been modified (i.e. <see cref="IsDirty" /> is <see langword="true" />.
    /// </summary>
    /// <returns>
    ///     <see langword="true" /> if the settings were saved successfully; otherwise,
    ///     <see langword="false" />.
    /// </returns>
    public bool SaveSettings();
}
