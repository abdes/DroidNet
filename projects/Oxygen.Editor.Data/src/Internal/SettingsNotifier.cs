// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using Oxygen.Editor.Data.Settings;

namespace Oxygen.Editor.Data.Internal;

/// <summary>
/// Manages observable notifications for setting changes.
/// </summary>
internal sealed class SettingsNotifier
{
    private readonly ConcurrentDictionary<string, object> typedSubjects = new(StringComparer.Ordinal);

    /// <summary>
    /// Generates a subject key for a setting.
    /// </summary>
    /// <param name="settingsModule">The settings module name.</param>
    /// <param name="name">The setting name.</param>
    /// <returns>A unique subject key string.</returns>
    public static string GenerateSubjectKey(string settingsModule, string name)
        => $"{settingsModule}:{name}";

    /// <summary>
    /// Gets or creates an observable for a specific setting key.
    /// </summary>
    /// <typeparam name="T">The type of the setting value.</typeparam>
    /// <param name="settingsModule">The settings module name.</param>
    /// <param name="name">The setting name.</param>
    /// <returns>An observable that emits change events for the setting.</returns>
    public IObservable<SettingChangedEvent<T>> GetObservable<T>(string settingsModule, string name)
    {
        var subjectKey = GenerateSubjectKey(settingsModule, name);
        var subject = (SimpleSubject<SettingChangedEvent<T>>)this.typedSubjects.GetOrAdd(
            subjectKey,
            _ => new SimpleSubject<SettingChangedEvent<T>>());
        return subject;
    }

    /// <summary>
    /// Notifies observers of a setting change.
    /// </summary>
    /// <typeparam name="T">The type of the setting value.</typeparam>
    /// <param name="settingsModule">The settings module name.</param>
    /// <param name="name">The setting name.</param>
    /// <param name="key">The typed setting key.</param>
    /// <param name="oldValue">The previous value.</param>
    /// <param name="newValue">The new value.</param>
    /// <param name="scope">The setting scope.</param>
    /// <param name="scopeId">Optional scope identifier.</param>
    public void NotifyChange<T>(
        string settingsModule,
        string name,
        SettingKey<T> key,
        T? oldValue,
        T? newValue,
        SettingScope scope,
        string? scopeId)
    {
        var subjectKey = GenerateSubjectKey(settingsModule, name);
        if (this.typedSubjects.TryGetValue(subjectKey, out var subjObj) &&
            subjObj is SimpleSubject<SettingChangedEvent<T>> subject)
        {
            subject.OnNext(new SettingChangedEvent<T>(key, oldValue, newValue!, scope, scopeId));
        }
    }
}
