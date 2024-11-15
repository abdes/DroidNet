// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Samples.Settings;

using System.ComponentModel;

/// <summary>
/// Provides an interface for managing settings with property change notification and disposable
/// functionality.
/// </summary>
/// <typeparam name="TSettings">The type of the settings.</typeparam>
public interface ISettingsService<out TSettings> : INotifyPropertyChanged, IDisposable
    where TSettings : class
{
    /// <summary>
    /// Gets a value indicating whether the settings have been modified.
    /// </summary>
    bool IsDirty { get; }

    /// <summary>
    /// Gets a snapshot of the current settings. Future modifications of the settings will not be
    /// reflected in the returned snapshot.
    /// </summary>
    /// <returns>A snapshot of the settings.</returns>
    TSettings GetSettingsSnapshot();

    /// <summary>
    /// Saves the current settings if they have been modified (i.e. <see cref="IsDirty" /> is <see langword="true" />.
    /// </summary>
    /// <returns><see langword="true" /> if the settings were saved successfully; otherwise,
    /// <see langword="false" />.</returns>
    bool SaveSettings();
}
