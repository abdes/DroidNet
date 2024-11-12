// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Core.Services;

using System.IO.Abstractions;
using DroidNet.Config;

public class OxygenPathFinder : PathFinder, IOxygenPathFinder
{
    private const string OxygenProjectsFolderName = "Oxygen Projects";

    public OxygenPathFinder(IFileSystem fs, Config config)
        : base(fs, config)
    {
        this.PersonalProjects = fs.Path.GetFullPath(Path.Combine(this.UserDocuments, OxygenProjectsFolderName));
        this.LocalProjects = Path.GetFullPath(Path.Combine(this.LocalAppData, OxygenProjectsFolderName));

        // Initialize the App Data Root if it is not already created
        _ = fs.Directory.CreateDirectory(this.Temp);
        _ = fs.Directory.CreateDirectory(this.LocalAppData);
        _ = fs.Directory.CreateDirectory(this.LocalAppState);
        _ = fs.Directory.CreateDirectory(this.LocalProjects);
        _ = fs.Directory.CreateDirectory(this.PersonalProjects);
    }

    public string PersonalProjects { get; }

    public string LocalProjects { get; }
}
