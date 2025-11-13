// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config;

/// <summary>
///     The `IPathFinder` interface provides a set of properties and methods to retrieve various system
///     and user environment paths, as well as application-specific paths. It also includes helper
///     methods to get configuration file paths.
/// </summary>
public interface IPathFinder
{
    /// <summary>
    ///     Gets the mode of the pathfinder.
    /// </summary>
    public string Mode { get; }

    /// <summary>
    ///     Gets the name of the application.
    /// </summary>
    public string ApplicationName { get; }

    /*
     * System paths.
     */

    /// <summary>
    ///     Gets the system root path.
    /// </summary>
    public string SystemRoot { get; }

    /// <summary>
    ///     Gets the temporary files path.
    /// </summary>
    public string Temp { get; }

    /*
     * User environment paths.
     */

    /// <summary>
    ///     Gets the user's desktop path.
    /// </summary>
    public string UserDesktop { get; }

    /// <summary>
    ///     Gets the user's downloads path.
    /// </summary>
    public string UserDownloads { get; }

    /// <summary>
    ///     Gets the user's home directory path.
    /// </summary>
    public string UserHome { get; }

    /// <summary>
    ///     Gets the user's documents path.
    /// </summary>
    public string UserDocuments { get; }

    /// <summary>
    ///     Gets the user's OneDrive path.
    /// </summary>
    public string? UserOneDrive { get; }

    /*
     * Common Application specific paths.
     */

    /// <summary>
    ///     Gets the program data path.
    /// </summary>
    public string ProgramData { get; }

    /// <summary>
    ///     Gets the local application data path.
    /// </summary>
    public string LocalAppData { get; }

    /// <summary>
    ///     Gets the local application state path.
    /// </summary>
    public string LocalAppState { get; }

    /*
     * Helper methods.
     */

    /// <summary>
    ///     Gets the configuration file path for the specified configuration file name.
    /// </summary>
    /// <param name="configFileName">The name of the configuration file.</param>
    /// <returns>The full path to the configuration file.</returns>
    public string GetConfigFilePath(string configFileName);

    /// <summary>
    ///     Gets the program-specific configuration file path for the specified configuration file name.
    /// </summary>
    /// <param name="configFileName">The name of the configuration file.</param>
    /// <returns>The full path to the program-specific configuration file.</returns>
    public string GetProgramConfigFilePath(string configFileName);
}
