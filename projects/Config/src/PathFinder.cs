// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.IO.Abstractions;
using DroidNet.Config.Helpers;

namespace DroidNet.Config;

/// <summary>
///     Provides methods to find various system, user add application paths.
/// </summary>
public class PathFinder : IPathFinder
{
    /// <summary>
    ///     In development mode, path resolution will use the build environment, using the application's assembly
    ///     directory as the base.
    /// </summary>
    public const string DevelopmentMode = "dev";

    /// <summary>
    ///     In real mode, the application is assumed to be installed following the windows application deployment
    ///     standards. <see cref="ProgramData" /> and <see cref="LocalAppData" /> will use different
    ///     base directories.
    /// </summary>
    public const string RealMode = "real";

    private const string ApplicationStateFolderName = ".state";

    private readonly IFileSystem fs;

    /// <summary>
    ///     Initializes a new instance of the <see cref="PathFinder" /> class.
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
        this.UserDocuments = Environment.GetFolderPath(Environment.SpecialFolder.Personal);
        this.UserDownloads = KnownFolders.GetKnownFolderPath(new Guid(0x374DE290, 0x123F, 0x4565, 0x91, 0x64, 0x39, 0xC4, 0x92, 0x5E, 0x46, 0x7B) /* 374DE290-123F-4565-9164-39C4925E467B */);

        // OneDrive is not guaranteed to be available on all systems
#pragma warning disable CA1031 // Do not catch general exception types
        try
        {
            this.UserOneDrive = KnownFolders.GetKnownFolderPath(new Guid(0xA52BBA46, 0xE9E1, 0x435F, 0xB3, 0xD9, 0x28, 0xDA, 0xA6, 0x48, 0xC0, 0xF6) /* A52BBA46-E9E1-435f-B3D9-28DAA648C0F6 */);
        }
        catch
        {
            this.UserOneDrive = null;
        }
#pragma warning restore CA1031 // Do not catch general exception types

        this.ProgramData = AppContext.BaseDirectory;

        var localAppDataSpecialFolder = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        this.LocalAppData = fs.Path.GetFullPath(
            Path.Combine(localAppDataSpecialFolder, pathFinderConfig.CompanyName, pathFinderConfig.ApplicationName));

        this.LocalAppState = Path.Combine(this.LocalAppData, ApplicationStateFolderName);
    }

    /// <inheritdoc />
    public string Mode { get; }

    /// <inheritdoc />
    public string ApplicationName { get; }

    /// <inheritdoc />
    public string UserDesktop { get; }

    /// <inheritdoc />
    public string UserDownloads { get; }

    /// <inheritdoc />
    public string UserHome { get; }

    /// <inheritdoc />
    public string UserDocuments { get; }

    /// <inheritdoc />
    public string SystemRoot { get; }

    /// <inheritdoc />
    public string? UserOneDrive { get; }

    /// <inheritdoc />
    public string Temp { get; }

    /// <inheritdoc />
    public string ProgramData { get; }

    /// <inheritdoc />
    public string LocalAppData { get; }

    /// <inheritdoc />
    public string LocalAppState { get; }

    /// <inheritdoc />
    public string GetConfigFilePath(string configFileName) => this.fs.Path.Combine(this.LocalAppData, configFileName);

    /// <inheritdoc />
    public string GetProgramConfigFilePath(string configFileName)
        => this.fs.Path.Combine(this.ProgramData, configFileName);
}
