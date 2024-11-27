// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Projects;

/// <summary>
/// Represents a quick save location with a name and a path.
/// </summary>
public class QuickSaveLocation(string name, string path)
{
    /// <summary>
    /// Gets or sets the path of the quick save location.
    /// </summary>
    public string Path { get; set; } = path;

    /// <summary>
    /// Gets or sets the name of the quick save location.
    /// </summary>
    public string Name { get; set; } = name;
}
