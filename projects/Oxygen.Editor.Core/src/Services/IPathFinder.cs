// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Core.Services;

public interface IPathFinder
{
    protected const string ApplicationStateFolderName = ".state";

    protected const string OxygenProjectsFolderName = "Oxygen Projects";

    /*
     * User environment paths.
     */

    string UserDesktop { get; }

    string UserDownloads { get; }

    string UserHome { get; }

    string SystemRoot { get; }

    string Temp { get; }

    /*
     * Oxygen Editor specific paths.
     */

    string PersonalProjects { get; }

    string LocalProjects { get; }

    string ProgramData { get; }

    string LocalAppData { get; }

    string LocalAppState { get; }
}
