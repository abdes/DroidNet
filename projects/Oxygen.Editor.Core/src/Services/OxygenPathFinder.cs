// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Core.Services;

using System.IO.Abstractions;
using DroidNet.Config;

public class OxygenPathFinder : IOxygenPathFinder
{
    private const string OxygenProjectsFolderName = "Oxygen Projects";

    private readonly IPathFinder parentFinder;

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

    public string PersonalProjects { get; }

    public string LocalProjects { get; }

    public string Mode => this.parentFinder.Mode;

    public string ApplicationName => this.parentFinder.ApplicationName;

    public string SystemRoot => this.parentFinder.SystemRoot;

    public string Temp => this.parentFinder.Temp;

    public string UserDesktop => this.parentFinder.UserDesktop;

    public string UserDownloads => this.parentFinder.UserDownloads;

    public string UserHome => this.parentFinder.UserHome;

    public string UserDocuments => this.parentFinder.UserDocuments;

    public string ProgramData => this.parentFinder.ProgramData;

    public string LocalAppData => this.parentFinder.LocalAppData;

    public string LocalAppState => this.parentFinder.LocalAppState;

    public string GetConfigFilePath(string configFileName)
        => this.parentFinder.GetConfigFilePath(configFileName);

    public string GetProgramConfigFilePath(string configFileName)
        => this.parentFinder.GetProgramConfigFilePath(configFileName);
}
