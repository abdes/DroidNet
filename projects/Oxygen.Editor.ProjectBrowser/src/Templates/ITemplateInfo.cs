// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Editor.World;

namespace Oxygen.Editor.ProjectBrowser.Templates;

/// <summary>
/// Defines the contract for project template information in the Oxygen Editor.
/// </summary>
/// <remarks>
/// <para>
/// Templates provide the structure and initial content for creating new projects.
/// Each template includes metadata like name, description, categorization, and preview assets.
/// </para>
/// </remarks>
public interface ITemplateInfo
{
    /// <summary>
    /// Gets or sets the localized display name of the template.
    /// </summary>
    public string Name { get; set; }

    /// <summary>
    /// Gets or sets the localized description explaining the purpose and contents of this template.
    /// </summary>
    public string Description { get; set; }

    /// <summary>
    /// Gets the category this template belongs to for organizational purposes.
    /// </summary>
    public Category Category { get; }

    /// <summary>
    /// Gets or sets the filesystem path where this template's content is stored.
    /// </summary>
    public string Location { get; set; }

    /// <summary>
    /// Gets or sets the path to an icon file representing this template in the UI.
    /// </summary>
    /// <remarks>
    /// Can be <see langword="null"/> if no custom icon is specified.
    /// </remarks>
    public string? Icon { get; set; }

    /// <summary>
    /// Gets the collection of paths to preview images showcasing the template.
    /// </summary>
    public IList<string> PreviewImages { get; }

    /// <summary>
    /// Gets or sets the timestamp when this template was last used to create a project.
    /// </summary>
    public DateTime LastUsedOn { get; set; }
}
