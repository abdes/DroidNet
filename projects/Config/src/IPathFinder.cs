// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config;

/// <summary>
/// The `IPathFinder` interface provides a set of properties and methods to retrieve various system
/// and user environment paths, as well as application-specific paths. It also includes helper
/// methods to get configuration file paths.
/// </summary>
public interface IPathFinder
{
    /// <summary>
    /// Gets the mode of the path finder.
    /// </summary>
    public string Mode { get; }

    /// <summary>
    /// Gets the name of the application.
    /// </summary>
    string ApplicationName { get; }

    /*
     * System paths.
     */

    /// <summary>
    /// Gets the system root path.
    /// </summary>
    string SystemRoot { get; }

    /// <summary>
    /// Gets the temporary files path.
    /// </summary>
    string Temp { get; }

    /*
     * User environment paths.
     */

    /// <summary>
    /// Gets the user's desktop path.
    /// </summary>
    string UserDesktop { get; }

    /// <summary>
    /// Gets the user's downloads path.
    /// </summary>
    string UserDownloads { get; }

    /// <summary>
    /// Gets the user's home directory path.
    /// </summary>
    string UserHome { get; }

    /// <summary>
    /// Gets the user's documents path.
    /// </summary>
    string UserDocuments { get; }

    /*
     * Common Application specific paths.
     */

    /// <summary>
    /// Gets the program data path.
    /// </summary>
    string ProgramData { get; }

    /// <summary>
    /// Gets the local application data path.
    /// </summary>
    string LocalAppData { get; }

    /// <summary>
    /// Gets the local application state path.
    /// </summary>
    string LocalAppState { get; }

    /*
     * Helper methods.
     */

    /// <summary>
    /// Gets the configuration file path for the specified configuration file name.
    /// </summary>
    /// <param name="configFileName">The name of the configuration file.</param>
    /// <returns>The full path to the configuration file.</returns>
    string GetConfigFilePath(string configFileName);

    /// <summary>
    /// Gets the program-specific configuration file path for the specified configuration file name.
    /// </summary>
    /// <param name="configFileName">The name of the configuration file.</param>
    /// <returns>The full path to the program-specific configuration file.</returns>
    string GetProgramConfigFilePath(string configFileName);
}
