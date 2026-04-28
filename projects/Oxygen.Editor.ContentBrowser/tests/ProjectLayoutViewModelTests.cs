// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentBrowser.ProjectExplorer;
using Oxygen.Editor.World;

namespace Oxygen.Editor.ContentBrowser.Tests;

[TestClass]
public sealed class ProjectLayoutViewModelTests
{
    [TestMethod]
    public void IsPersistedProjectRelativeVirtualMount_WhenDerivedRoot_ShouldReturnTrue()
    {
        Assert.IsTrue(ProjectLayoutViewModel.IsPersistedProjectRelativeVirtualMount(new ProjectMountPoint("Cooked", ".cooked")));
        Assert.IsTrue(ProjectLayoutViewModel.IsPersistedProjectRelativeVirtualMount(new ProjectMountPoint("Imported", ".imported")));
        Assert.IsTrue(ProjectLayoutViewModel.IsPersistedProjectRelativeVirtualMount(new ProjectMountPoint("Build", ".build")));
    }

    [TestMethod]
    public void IsPersistedProjectRelativeVirtualMount_WhenAuthoringMount_ShouldReturnFalse()
    {
        Assert.IsFalse(ProjectLayoutViewModel.IsPersistedProjectRelativeVirtualMount(new ProjectMountPoint("Content", "Content")));
        Assert.IsFalse(ProjectLayoutViewModel.IsPersistedProjectRelativeVirtualMount(new ProjectMountPoint("Materials", "Content/Materials")));
    }
}
