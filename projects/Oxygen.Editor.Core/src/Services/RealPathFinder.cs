// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Core.Services;

using System.IO.Abstractions;
using System.Runtime.InteropServices;

public class RealPathFinder : IPathFinder
{
    public RealPathFinder(IFileSystem fs)
    {
        this.UserDesktop = Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);
        this.UserHome = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
        this.UserDownloads = SHGetKnownFolderPath(new Guid("374DE290-123F-4565-9164-39C4925E467B"), 0);

        this.SystemRoot = Environment.GetFolderPath(Environment.SpecialFolder.System);
        this.Temp = Path.GetTempPath();

        this.ProgramData = AppContext.BaseDirectory;

        var folderBase = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        this.LocalAppData = Path.GetFullPath(Path.Combine(folderBase, Constants.Company, Constants.Application));
        this.LocalAppState = Path.Combine(this.LocalAppData, IPathFinder.ApplicationStateFolderName);

        this.PersonalProjects = Path.GetFullPath(
            Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.Personal),
                IPathFinder.OxygenProjectsFolderName));

        this.LocalProjects = Path.GetFullPath(Path.Combine(this.LocalAppData, IPathFinder.OxygenProjectsFolderName));

        // Initialize the App Data Root if it is not already created
        _ = fs.Directory.CreateDirectory(this.Temp);
        _ = fs.Directory.CreateDirectory(this.LocalAppData);
        _ = fs.Directory.CreateDirectory(this.LocalAppState);
        _ = fs.Directory.CreateDirectory(this.LocalProjects);
        _ = fs.Directory.CreateDirectory(this.PersonalProjects);
    }

    public string UserDesktop { get; }

    public string UserDownloads { get; }

    public string UserHome { get; }

    public string SystemRoot { get; }

    public string Temp { get; }

    public string PersonalProjects { get; }

    public string LocalProjects { get; }

    public string LocalAppData { get; }

    public string LocalAppState { get; }

    public string ProgramData { get; }

#pragma warning disable SYSLIB1054 // Use 'LibraryImportAttribute' instead of 'DllImportAttribute' to generate P/Invoke marshalling code at compile time
    [DllImport("shell32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, PreserveSig = false)]
    private static extern string SHGetKnownFolderPath(
        [MarshalAs(UnmanagedType.LPStruct)] Guid refToGuid,
        uint dwFlags,
        IntPtr hToken = default);
#pragma warning restore SYSLIB1054 // Use 'LibraryImportAttribute' instead of 'DllImportAttribute' to generate P/Invoke marshalling code at compile time
}
