// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentExplorer.Panes.ContentFolders;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentExplorer.Tests;

[TestClass]
public sealed class ContentFoldersViewModelTests
{
    [TestMethod]
    public void IsPersistedProjectRelativeVirtualMount_WhenDerivedRoot_ShouldReturnTrue()
    {
        Assert.IsTrue(ContentFoldersViewModel.IsPersistedProjectRelativeVirtualMount(new ProjectMountPoint("Cooked", ".cooked")));
        Assert.IsTrue(ContentFoldersViewModel.IsPersistedProjectRelativeVirtualMount(new ProjectMountPoint("Imported", ".imported")));
        Assert.IsTrue(ContentFoldersViewModel.IsPersistedProjectRelativeVirtualMount(new ProjectMountPoint("Build", ".build")));
    }

    [TestMethod]
    public void IsPersistedProjectRelativeVirtualMount_WhenAuthoringMount_ShouldReturnFalse()
    {
        Assert.IsFalse(ContentFoldersViewModel.IsPersistedProjectRelativeVirtualMount(new ProjectMountPoint("Content", "Content")));
        Assert.IsFalse(ContentFoldersViewModel.IsPersistedProjectRelativeVirtualMount(new ProjectMountPoint("Materials", "Content/Materials")));
    }
}
