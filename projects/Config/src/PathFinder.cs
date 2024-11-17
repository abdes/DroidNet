// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.IO.Abstractions;

namespace DroidNet.Config;

/// <summary>
/// Provides methods to find various system, user add application paths.
/// </summary>
public class PathFinder : IPathFinder
{
    /// <summary>
    /// In development mode, path resolution will use the build environment, using the application's assembly
    /// directory as the base.
    /// </summary>
    public const string DevelopmentMode = "dev";

    /// <summary>
    /// In real mode, the application is assumed to be installed following the windows application deployment
    /// standards. <see cref="ProgramData" /> and <see cref="LocalAppData" /> will use different
    /// base directories.
    /// </summary>
    public const string RealMode = "real";

    private const string ApplicationStateFolderName = ".state";

    private readonly IFileSystem fs;

    /// <summary>
    /// Initializes a new instance of the <see cref="PathFinder"/> class.
    /// </summary>
    /// <param name="fs">The file system abstraction to use for path operations.</param>
    /// <param name="pathFinderConfig">The configuration settings for the PathFinder.</param>
    public PathFinder(IFileSystem fs, PathFinderConfig pathFinderConfig)
    {
        this.fs = fs;

        // Anything other than development is interpreted as RealMode
        this.Mode = string.Equals(pathFinderConfig.Mode, DevelopmentMode, StringComparison.OrdinalIgnoreCase)
            ? DevelopmentMode
            : RealMode;

        this.ApplicationName = pathFinderConfig.ApplicationName;

        this.SystemRoot = Environment.GetFolderPath(Environment.SpecialFolder.System);
        this.Temp = Path.GetTempPath();

        this.UserDesktop = Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);
        this.UserHome = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
        this.UserDownloads = KnownFolderPathHelpers.SHGetKnownFolderPath(
            new Guid("374DE290-123F-4565-9164-39C4925E467B"),
            0);
        this.UserDocuments = Environment.GetFolderPath(Environment.SpecialFolder.Personal);

        this.ProgramData = AppContext.BaseDirectory;

        var localAppDataSpecialFolder = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        var localAppData = fs.Path.GetFullPath(
            Path.Combine(localAppDataSpecialFolder, pathFinderConfig.CompanyName, pathFinderConfig.ApplicationName));
        this.LocalAppData = this.Mode switch
        {
            DevelopmentMode => fs.Path.GetFullPath(fs.Path.Combine(localAppData, "Development")),
            _ => localAppData,
        };

        this.LocalAppState = Path.Combine(this.LocalAppData, ApplicationStateFolderName);
    }

    /// <inheritdoc/>
    public string Mode { get; }

    /// <inheritdoc/>
    public string ApplicationName { get; }

    /// <inheritdoc/>
    public string UserDesktop { get; }

    /// <inheritdoc/>
    public string UserDownloads { get; }

    /// <inheritdoc/>
    public string UserHome { get; }

    /// <inheritdoc/>
    public string UserDocuments { get; }

    /// <inheritdoc/>
    public string SystemRoot { get; }

    /// <inheritdoc/>
    public string Temp { get; }

    /// <inheritdoc/>
    public string ProgramData { get; }

    /// <inheritdoc/>
    public string LocalAppData { get; }

    /// <inheritdoc/>
    public string LocalAppState { get; }

    /// <inheritdoc/>
    public string GetConfigFilePath(string configFileName) => this.fs.Path.Combine(this.LocalAppData, configFileName);

    /// <inheritdoc/>
    public string GetProgramConfigFilePath(string configFileName)
        => this.fs.Path.Combine(this.ProgramData, configFileName);
}
