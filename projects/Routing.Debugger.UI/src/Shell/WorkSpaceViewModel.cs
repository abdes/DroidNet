// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.Shell;

using System.Diagnostics;
using DroidNet.Docking;
using DroidNet.Docking.Detail;

public class WorkSpaceViewModel
{
    public WorkSpaceViewModel()
    {
        this.Docker.DockToRoot(CreateToolDockOrFail());

        this.Docker.DockToRoot(CreateToolDockOrFail(), AnchorPosition.Left);
        this.Docker.DockToRoot(CreateToolDockOrFail(), AnchorPosition.Left);
        this.Docker.DockToRoot(CreateToolDockOrFail(), AnchorPosition.Left);
        this.Docker.DockToRoot(CreateToolDockOrFail(), AnchorPosition.Bottom);

        DumpGroup(this.Docker.Root);

        this.Root = new DockGroupViewModel(this.Docker.Root);
    }

    public Docker Docker { get; } = new();

    public DockGroupViewModel Root { get; }

    private static ToolDock CreateToolDockOrFail()
        => ToolDock.New() ?? throw new Exception("could not create dock");

    private static void DumpGroup(IDockGroup group, string indent = "")
    {
        Debug.WriteLine($"{indent}{group}");
        if (group.First is not null)
        {
            DumpGroup(group.First, indent + "   ");
        }

        if (group.Second is not null)
        {
            DumpGroup(group.Second, indent + "   ");
        }
    }
}
