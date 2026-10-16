// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets.Catalog;

/// <summary>A catalog that knows the folders of its roots and reports when they change.</summary>
/// <remarks>
/// A folder created, deleted, renamed or moved outside the editor reaches a file watcher as one event for the folder,
/// not one per file. The catalog reads its root again, so the files inside are added or removed too, and reports the
/// folder here: views that show folders follow it without watching the disk themselves.
/// </remarks>
public interface IAssetFolderSource
{
    /// <summary>
    /// Gets the folders created or deleted under the catalog's roots, by virtual folder URI such as
    /// <c>asset:///Content/Materials</c>. A rename or move reports the old folder and the new one.
    /// </summary>
    public IObservable<Uri> FolderChanges { get; }
}
