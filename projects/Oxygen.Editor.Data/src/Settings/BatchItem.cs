// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Data.Settings;

/// <summary>
/// Represents a single item in a settings batch.
/// </summary>
/// <param name="Module">The settings module name.</param>
/// <param name="Name">The setting name.</param>
/// <param name="Value">The setting value.</param>
/// <param name="Scope">The setting scope.</param>
/// <param name="ScopeId">The scope identifier (null for Application scope, required for Project scope).</param>
/// <param name="Descriptor">Descriptor for validation.</param>
internal sealed record BatchItem(
    string Module,
    string Name,
    object? Value,
    SettingScope Scope,
    string? ScopeId,
    ISettingDescriptor Descriptor);
