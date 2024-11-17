// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Docking;

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

/// <summary>
/// Represents a dock that holds only one dockable. Any attempt to add a
/// <see cref="IDockable" /> to it will result in replacing the current one.
/// </summary>
public partial class ApplicationDock : SingleItemDock
{
    /// <summary>
    /// Initializes a new instance of the <see cref="ApplicationDock"/> class.
    /// </summary>
    protected ApplicationDock()
    {
    }

    /// <inheritdoc/>
    public override bool CanMinimize => false;

    /// <inheritdoc/>
    public override bool CanClose => false;

    /// <summary>
    /// Gets the ViewModel of the first dockable in the dock.
    /// </summary>
    public object? ApplicationViewModel => this.Dockables.FirstOrDefault()?.ViewModel;

    /// <summary>
    /// Creates a new instance of the <see cref="ApplicationDock"/> class.
    /// </summary>
    /// <returns>A new instance of <see cref="ApplicationDock"/> if successful; otherwise, null.</returns>
    public static new ApplicationDock? New() => Factory.CreateDock(typeof(ApplicationDock)) as ApplicationDock;
}
