// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

namespace DroidNet.Config.Tests.TestHelpers;

[ExcludeFromCodeCoverage]
internal sealed class TestPathFinder : IPathFinder
{
    private readonly string resolvedPath;

    public TestPathFinder(string resolvedPath)
    {
        this.resolvedPath = resolvedPath;
    }

    public string Mode => string.Empty;

    public string ApplicationName => string.Empty;

    public string SystemRoot => string.Empty;

    public string Temp => string.Empty;

    public string UserDesktop => string.Empty;

    public string UserDownloads => string.Empty;

    public string UserHome => string.Empty;

    public string UserDocuments => string.Empty;

    public string UserOneDrive => string.Empty;

    public string ProgramData => string.Empty;

    public string LocalAppData => string.Empty;

    public string LocalAppState => string.Empty;

    public string GetConfigFilePath(string configFileName)
    {
        return this.resolvedPath;
    }

    public string GetProgramConfigFilePath(string configFileName)
    {
        return this.resolvedPath;
    }
}
