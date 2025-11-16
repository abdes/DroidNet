// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Data.Settings;

/// <summary>
/// Represents a change notification for a typed setting.
/// </summary>
/// <typeparam name="T">The setting value type.</typeparam>
/// <param name="Key">The typed setting key.</param>
/// <param name="OldValue">The previous value (null if none).</param>
/// <param name="NewValue">The new value.</param>
/// <param name="Scope">The scope where the change occurred.</param>
/// <param name="ScopeId">Optional scope identifier.</param>
public readonly record struct SettingChangedEvent<T>(
    SettingKey<T> Key,
    T? OldValue,
    T NewValue,
    SettingScope Scope,
    string? ScopeId);
