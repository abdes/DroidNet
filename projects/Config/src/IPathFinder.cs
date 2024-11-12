// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config;

public interface IPathFinder
{
    string Mode { get; }

    string ApplicationName { get; }

    /*
     * System paths.
     */

    string SystemRoot { get; }

    string Temp { get; }

    /*
     * User environment paths.
     */

    string UserDesktop { get; }

    string UserDownloads { get; }

    string UserHome { get; }

    string UserDocuments { get; }

    /*
     * Common Application specific paths.
     */

    string ProgramData { get; }

    string LocalAppData { get; }

    string LocalAppState { get; }

    /*
     * Helper methods.
     */

    string GetConfigFilePath(string configFileName);

    string GetProgramConfigFilePath(string configFileName);
}
