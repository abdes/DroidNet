// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects.Config;

using System.Diagnostics;
using Oxygen.Editor.Projects;

/// <summary>Configuration settings for the project browser.</summary>
/// These settings can be obtained only through the DI injector. They are
/// loaded from one of the application configuration files during the
/// application startup. They need to be placed within a section named
/// `ConfigSectionName`.
public class ProjectsSettings
{
    /// <summary>
    /// The name of the configuration section under which the project browser settings
    /// must appear.
    /// </summary>
    public static readonly string ConfigSectionName = "ProjectsSettings";

    private readonly Dictionary<string, ProjectCategory> categories = [];

    /// <summary>
    /// Gets the list of supported template categories.
    /// <remarks>
    /// The list is initialized during the DI injection.
    /// </remarks>
    /// </summary>
    /// <value>The list of supported template categories.</value>
    public IList<ProjectCategory> Categories
    {
        get => [.. this.categories.Values];
        init
        {
            foreach (var category in value)
            {
                Debug.Assert(
                    !this.categories.ContainsKey(category.Id),
                    $"Attempt to add a category (name=`{category.Name}`) with duplicate Id `{category.Id}`");
                this.categories[category.Id] = category;
            }
        }
    }

    /// <summary>Get the project category with the given ID.</summary>
    /// <param name="id">the category id.</param>
    /// <returns>
    /// a valid ProjectCategory object if there is one with the given id; null
    /// otherwise.
    /// </returns>
    public ProjectCategory? GetProjectCategoryById(string id)
        => this.categories.TryGetValue(id, out var value) ? value : null;
}
