// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

using System.ComponentModel;
using System.Diagnostics;
using DroidNet.Controls;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Storage;

public partial class FolderTreeItemAdapter : TreeItemAdapter
{
    private readonly ILogger logger;
    private readonly Task<IFolder> folderAsync;
    private readonly string name;

    public FolderTreeItemAdapter(ILogger logger, Task<IFolder> folderAsync, string name)
    {
        this.logger = logger;
        this.folderAsync = folderAsync;
        this.name = name;

        this.ChildrenCollectionChanged += (_, _) => this.OnPropertyChanged(nameof(this.IconGlyph));
    }

    public override string Label => this.IsRoot ? $"{this.name} (Project Root)" : this.name;

    public string IconGlyph => this.IsExpanded && this.ChildrenCount > 0 ? "\uE838" : "\uE8B7";

    protected override int GetChildrenCount()
    {
        Debug.Fail("should never be called");

        // The entire project tree should be expanded when the view is loaded. As a result, the Children.Count will be
        // used and this method should not be called. As a precautionary measure, we'll simply return 1, indicating that
        // the item may have children and can be expanded. Once expanded, the real count of the children will be
        // updated.
        return 1;
    }

    protected override async Task LoadChildren()
    {
        var folder = await this.folderAsync.ConfigureAwait(true);
        await foreach (var child in folder.GetFoldersAsync().ConfigureAwait(true))
        {
            try
            {
                this.AddChildInternal(
                    new FolderTreeItemAdapter(this.logger, Task.FromResult(child), child.Name)
                    {
                        // IMPORTANT: The entire tree should be expanded to avoid calls to GetChildrenCount and to be able
                        // To mark selected items specified in the ActiveRoute query params
                        IsExpanded = true,
                        Depth = this.Depth + 1,
                    });
            }
            catch (Exception ex)
            {
                CouldNotLoadProjectFolders(this.logger, folder.Location, ex.Message);
            }
        }
    }

    protected override void OnPropertyChanged(PropertyChangedEventArgs e)
    {
        base.OnPropertyChanged(e);
        if (e.PropertyName?.Equals(nameof(this.IsExpanded), StringComparison.Ordinal) == true)
        {
            this.OnPropertyChanged(nameof(this.IconGlyph));
        }
    }

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "An error occurred while loading project folders from `{location}`: {error}")]
    private static partial void CouldNotLoadProjectFolders(ILogger logger, string location, string error);
}
