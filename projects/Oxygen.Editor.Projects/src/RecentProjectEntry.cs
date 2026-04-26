// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.Projects;

/// <summary>
///     Recent project record with validation state.
/// </summary>
public sealed record RecentProjectEntry
{
    /// <summary>
    ///     Gets the stored project display name.
    /// </summary>
    public required string Name { get; init; }

    /// <summary>
    ///     Gets the stored project location.
    /// </summary>
    public required string Location { get; init; }

    /// <summary>
    ///     Gets the last time the project was used.
    /// </summary>
    public required DateTime LastUsedOn { get; init; }

    /// <summary>
    ///     Gets the validation result for this recent project.
    /// </summary>
    public required ProjectValidationResult Validation { get; init; }

    /// <summary>
    ///     Gets the loaded project info when validation succeeds.
    /// </summary>
    public IProjectInfo? ProjectInfo => this.Validation.ProjectInfo;

    /// <summary>
    ///     Gets a value indicating whether this recent project can be opened.
    /// </summary>
    public bool IsUsable => this.Validation.IsValid;
}
