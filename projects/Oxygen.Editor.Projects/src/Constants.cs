// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

/// <summary>
/// Provides constant values used throughout the Oxygen Editor Projects module.
/// </summary>
/// <remarks>
/// This class contains constant values that are used for file naming and directory structure within the Oxygen Editor Projects module.
/// These constants help ensure consistency and avoid hardcoding strings throughout the codebase.
/// </remarks>
public static class Constants
{
    /// <summary>
    /// The default file name for a project file.
    /// </summary>
    /// <remarks>
    /// This constant represents the default file name for a project file within the Oxygen Editor.
    /// </remarks>
    public const string ProjectFileName = "Project.oxy";

    /// <summary>
    /// The name of the folder where scenes are stored.
    /// </summary>
    /// <remarks>
    /// This constant represents the name of the folder where scene files are stored within a project directory.
    /// </remarks>
    public const string ScenesFolderName = "Scenes";

    /// <summary>
    /// The file extension for scene files.
    /// </summary>
    /// <remarks>
    /// This constant represents the file extension used for scene files within the Oxygen Editor.
    /// </remarks>
    public const string SceneFileExtension = ".scene";
}
