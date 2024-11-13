// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config;

using System.IO.Abstractions;

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

    public string Mode { get; }

    public string ApplicationName { get; }

    public string UserDesktop { get; }

    public string UserDownloads { get; }

    public string UserHome { get; }

    public string UserDocuments { get; }

    public string SystemRoot { get; }

    public string Temp { get; }

    public string ProgramData { get; }

    public string LocalAppData { get; }

    public string LocalAppState { get; }

    public string GetConfigFilePath(string configFileName) => this.fs.Path.Combine(this.LocalAppData, configFileName);

    public string GetProgramConfigFilePath(string configFileName)
        => this.fs.Path.Combine(this.ProgramData, configFileName);
}
