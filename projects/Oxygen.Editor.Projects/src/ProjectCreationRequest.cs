// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.Projects;

/// <summary>
///     Request to create a project from a template payload.
/// </summary>
public sealed record ProjectCreationRequest
{
    /// <summary>
    ///     Gets the template root folder to copy.
    /// </summary>
    public required string TemplateRoot { get; init; }

    /// <summary>
    ///     Gets the new project name.
    /// </summary>
    public required string ProjectName { get; init; }

    /// <summary>
    ///     Gets the parent folder where the project folder will be created.
    /// </summary>
    public required string ParentLocation { get; init; }

    /// <summary>
    ///     Gets the fallback category used when the template does not provide a manifest.
    /// </summary>
    public required Category Category { get; init; }

    /// <summary>
    ///     Gets the optional fallback thumbnail path.
    /// </summary>
    public string? Thumbnail { get; init; }
}
