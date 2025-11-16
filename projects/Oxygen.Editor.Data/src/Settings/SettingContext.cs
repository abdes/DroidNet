// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Data.Settings;

/// <summary>
/// Defines the scope of a setting.
/// </summary>
public enum SettingScope
{
    /// <summary>
    /// Global application-level setting.
    /// </summary>
    Application = 0,

    /// <summary>
    /// Project specific setting.
    /// </summary>
    Project = 1,
}

/// <summary>
/// Describes a setting resolution context (scope + optional identifier).
/// </summary>
public sealed record SettingContext(SettingScope Scope, string? ScopeId = null)
{
    /// <summary>
    /// Creates an application-level context.
    /// </summary>
    /// <returns>A <see cref="SettingContext"/> representing the application-level scope.</returns>
    public static SettingContext Application() => new(SettingScope.Application);

    /// <summary>
    /// Creates a project-level context using the provided project path as the scope id.
    /// </summary>
    /// <param name="projectPath">The path identifying the project scope.</param>
    /// <returns>A <see cref="SettingContext"/> representing the project-level scope.</returns>
    public static SettingContext Project(string projectPath) => new(SettingScope.Project, projectPath);
}
