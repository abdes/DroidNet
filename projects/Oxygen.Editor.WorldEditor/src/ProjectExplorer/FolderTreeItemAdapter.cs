// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics;
using DroidNet.Controls;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Core;
using Oxygen.Editor.Storage;

namespace Oxygen.Editor.World.ProjectExplorer;

/// <summary>
///     Represents a folder item in the content browser's tree structure.
/// </summary>
public partial class FolderTreeItemAdapter : TreeItemAdapter, IDisposable
{
    private readonly ILogger logger;
    private bool disposed;

    private string label;

    /// <summary>
    ///     Initializes a new instance of the <see cref="FolderTreeItemAdapter" /> class.
    /// </summary>
    /// <param name="logger">The logger to use for logging errors.</param>
    /// <param name="folder">The folder represented by this adapter.</param>
    /// <param name="label">The label for the folder.</param>
    /// <param name="isRoot">Indicates if this folder is the root folder.</param>
    /// <param name="isHidden">Indicates if this folder is hidden.</param>
    public FolderTreeItemAdapter(
        ILogger logger,
        IFolder folder,
        string label,
        bool isRoot = false,
        bool isHidden = false)
        : base(isRoot, isHidden)
    {
        this.logger = logger;
        this.Folder = folder;
        this.label = label;

        this.ChildrenCollectionChanged += (_, _) => this.OnPropertyChanged(nameof(this.IconGlyph));
    }

    /// <inheritdoc />
    public override string Label
    {
        get => this.IsRoot ? $"{this.label} (Project Root)" : this.label;
        set
        {
            if (string.Equals(value, this.label, StringComparison.Ordinal))
            {
                return;
            }

            this.label = value;
            this.OnPropertyChanged();
        }
    }

    /// <summary>
    ///     Gets the icon glyph for the folder.
    /// </summary>
    public string IconGlyph => this.IsExpanded && this.ChildrenCount > 0 ? "\uE838" : "\uE8B7";

    /// <summary>
    ///     Gets the folder represented by this adapter.
    /// </summary>
    public IFolder Folder { get; }

    /// <summary>
    ///     Disposes the resources used by the <see cref="FolderTreeItemAdapter" />.
    /// </summary>
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <inheritdoc />
    public override bool ValidateItemName(string name) => InputValidation.IsValidFileName(name);

    /// <inheritdoc />
    protected override int DoGetChildrenCount()
    {
        Debug.Fail("should never be called");

        // The entire project tree should be expanded when the view is loaded. As a result, the Children.Count will be
        // used and this method should not be called. As a precautionary measure, we'll simply return 1, indicating that
        // the item may have children and can be expanded. Once expanded, the real count of the children will be
        // updated.
        return 1;
    }

    /// <inheritdoc />
    protected override async Task LoadChildren()
    {
        await foreach (var child in this.Folder.GetFoldersAsync().ConfigureAwait(true).ConfigureAwait(false)
                           .ConfigureAwait(false))
        {
            FolderTreeItemAdapter? item = null;
            try
            {
                // IMPORTANT: The entire tree should be expanded to avoid calls to GetChildrenCount and to be able
                // To mark selected items specified in the ActiveRoute query params
                item = new FolderTreeItemAdapter(this.logger, child, child.Name)
                {
                    IsExpanded = true,
                };

                this.AddChildInternal(item);
                item = null;
            }
#pragma warning disable CA1031 // exceptions will not stop the loading of the rest of the folders
            catch (Exception ex)
            {
                // Log the failure, but continue with the rest
                this.CouldNotLoadProjectFolders(this.Folder.Location, ex.Message);
            }
#pragma warning restore CA1031
            finally
            {
                item?.Dispose();
            }
        }
    }

    /// <inheritdoc />
    protected override void OnPropertyChanged(PropertyChangedEventArgs e)
    {
        base.OnPropertyChanged(e);
        if (e.PropertyName?.Equals(nameof(this.IsExpanded), StringComparison.Ordinal) == true)
        {
            this.OnPropertyChanged(nameof(this.IconGlyph));
        }

        // FolderTreeItemAdapter is now a pure tree item - it doesn't update any external state
        // The ProjectLayoutViewModel will listen to tree selection changes and update ContentBrowserState
    }

    /// <summary>
    ///     Disposes the resources used by the <see cref="FolderTreeItemAdapter" />.
    /// </summary>
    /// <param name="disposing">A value indicating whether the method is called from Dispose.</param>
    protected virtual void Dispose(bool disposing)
    {
        if (!this.disposed && disposing)
        {
            // No more event subscriptions to clean up
            this.disposed = true;
        }
    }

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "An error occurred while loading project folders from `{location}`: {error}")]
    private partial void CouldNotLoadProjectFolders(string location, string error);
}
