// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Data.Settings;

/// <summary>
/// Represents a value persisted under a scope and scope id.
/// </summary>
/// <typeparam name="T">The value type.</typeparam>
public readonly record struct ScopedValue<T>(SettingScope Scope, string? ScopeId, T? Value);
