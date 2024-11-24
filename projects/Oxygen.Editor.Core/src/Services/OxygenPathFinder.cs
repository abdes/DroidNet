// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.IO.Abstractions;
using DroidNet.Config;

namespace Oxygen.Editor.Core.Services;

/// <summary>
/// Provides methods to find paths related to Oxygen projects and application data.
/// </summary>
public class OxygenPathFinder : IOxygenPathFinder
{
    private const string OxygenProjectsFolderName = "Oxygen Projects";

    private readonly IPathFinder parentFinder;

    /// <summary>
    /// Initializes a new instance of the <see cref="OxygenPathFinder"/> class.
    /// </summary>
    /// <param name="parentFinder">The parent path finder.</param>
    /// <param name="fs">The file system abstraction.</param>
    public OxygenPathFinder(IPathFinder parentFinder, IFileSystem fs)
    {
        this.parentFinder = parentFinder;
        this.PersonalProjects = fs.Path.GetFullPath(Path.Combine(parentFinder.UserDocuments, OxygenProjectsFolderName));
        this.LocalProjects = Path.GetFullPath(Path.Combine(parentFinder.LocalAppData, OxygenProjectsFolderName));

        // Initialize the App Data Root if it is not already created
        _ = fs.Directory.CreateDirectory(parentFinder.Temp);
        _ = fs.Directory.CreateDirectory(parentFinder.LocalAppData);
        _ = fs.Directory.CreateDirectory(parentFinder.LocalAppState);
        _ = fs.Directory.CreateDirectory(this.LocalProjects);
        _ = fs.Directory.CreateDirectory(this.PersonalProjects);
    }

    /// <inheritdoc/>
    public string PersonalProjects { get; }

    /// <inheritdoc/>
    public string LocalProjects { get; }

    /// <inheritdoc/>
    public string Mode => this.parentFinder.Mode;

    /// <inheritdoc/>
    public string ApplicationName => this.parentFinder.ApplicationName;

    /// <inheritdoc/>
    public string SystemRoot => this.parentFinder.SystemRoot;

    /// <inheritdoc/>
    public string Temp => this.parentFinder.Temp;

    /// <inheritdoc/>
    public string UserDesktop => this.parentFinder.UserDesktop;

    /// <inheritdoc/>
    public string UserDownloads => this.parentFinder.UserDownloads;

    /// <inheritdoc/>
    public string UserHome => this.parentFinder.UserHome;

    /// <inheritdoc/>
    public string UserDocuments => this.parentFinder.UserDocuments;

    /// <inheritdoc/>
    public string ProgramData => this.parentFinder.ProgramData;

    /// <inheritdoc/>
    public string LocalAppData => this.parentFinder.LocalAppData;

    /// <inheritdoc/>
    public string LocalAppState => this.parentFinder.LocalAppState;

    /// <inheritdoc/>
    public string GetConfigFilePath(string configFileName)
        => this.parentFinder.GetConfigFilePath(configFileName);

    /// <inheritdoc/>
    public string GetProgramConfigFilePath(string configFileName)
        => this.parentFinder.GetProgramConfigFilePath(configFileName);
}
