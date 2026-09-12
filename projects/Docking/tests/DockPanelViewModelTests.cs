// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Docking.Controls;
using DroidNet.Docking.Workspace;

namespace DroidNet.Docking.Tests;

[TestClass]
public sealed class DockPanelViewModelTests
{
    [TestMethod]
    public void ActivationSynchronizesTheModelAndResumesAfterReactivation()
    {
        using var docker = new Docker();
        var dock = ToolDock.New();
        var browser = Dockable.New(Guid.NewGuid().ToString("N"));
        var logs = Dockable.New(Guid.NewGuid().ToString("N"));
        var cooking = Dockable.New(Guid.NewGuid().ToString("N"));
        dock.AdoptDockable(browser);
        dock.AdoptDockable(logs);
        dock.AdoptDockable(cooking);
        var anchor = new AnchorBottom();
        try
        {
            docker.Dock(dock, anchor);
            anchor = null;
        }
        finally
        {
            anchor?.Dispose();
        }

        browser.IsActive = true;
        var model = new DockPanelViewModel(dock) { IsActive = true };
        try
        {
            cooking.IsActive = true;
            _ = model.ActiveDockable.Should().Be(cooking);
            _ = dock.Dockables.Count(value => value.IsActive).Should().Be(1);
            model.ActiveDockable = logs;
            _ = dock.ActiveDockable.Should().Be(logs);
            _ = dock.Dockables.Count(value => value.IsActive).Should().Be(1);
            model.IsActive = false;
            browser.IsActive = true;
            _ = model.ActiveDockable.Should().Be(logs);
            model.IsActive = true;
            _ = model.ActiveDockable.Should().Be(browser);
        }
        finally
        {
            model.IsActive = false;
        }
    }
}
