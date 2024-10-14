// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using DroidNet.Docking.Detail;

/// <summary>
/// A type of <see cref="Dock" /> most suitable for non-document dockable views.
/// </summary>
public partial class ToolDock : Dock
{
    protected ToolDock()
    {
    }

    public static ToolDock New() => (ToolDock)Factory.CreateDock(typeof(ToolDock));
}
