// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;

namespace DroidNet.Config;

/// <summary>
/// Provides the non-generic contract used by the settings manager to coordinate services.
/// </summary>
public interface ISettingsService : INotifyPropertyChanged, IDisposable
{
    /// <summary>
    /// Gets the section name used to identify this settings type in storage sources.
    /// </summary>
    public string SectionName { get; }

    /// <summary>
    /// Gets the concrete POCO type used for deserialization from storage.
    /// </summary>
    public Type SettingsType { get; }

    /// <summary>
    /// Applies properties from cached data (POCO or JsonElement) to this service's settings.
    /// Used by the SettingsManager to update the service when sources change.
    /// </summary>
    /// <param name="data">The cached data to apply. Can be null to reset to defaults.</param>
    public void ApplyProperties(object? data);
}
