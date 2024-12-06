// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics;
using DroidNet.Controls;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Core;
using Oxygen.Editor.Storage;

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

/// <summary>
/// Represents a folder item in the content browser's tree structure.
/// </summary>
public partial class FolderTreeItemAdapter : TreeItemAdapter
{
    private readonly ILogger logger;
    private readonly ContentBrowserState contentBrowserState;
    private readonly IFolder folder;

    private string label;

    /// <summary>
    /// Initializes a new instance of the <see cref="FolderTreeItemAdapter"/> class.
    /// </summary>
    /// <param name="logger">The logger to use for logging errors.</param>
    /// <param name="contentBrowserState">The state of the content browser.</param>
    /// <param name="folder">The folder represented by this adapter.</param>
    /// <param name="label">The label for the folder.</param>
    /// <param name="isRoot">Indicates if this folder is the root folder.</param>
    /// <param name="isHidden">Indicates if this folder is hidden.</param>
    public FolderTreeItemAdapter(
        ILogger logger,
        ContentBrowserState contentBrowserState,
        IFolder folder,
        string label,
        bool isRoot = false,
        bool isHidden = false)
        : base(isRoot, isHidden)
    {
        this.logger = logger;
        this.contentBrowserState = contentBrowserState;
        this.folder = folder;
        this.label = label;

        this.RestoreFromContentBrowserState();

        this.ChildrenCollectionChanged += (_, _) => this.OnPropertyChanged(nameof(this.IconGlyph));
    }

    /// <inheritdoc/>
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
    /// Gets the icon glyph for the folder.
    /// </summary>
    public string IconGlyph => this.IsExpanded && this.ChildrenCount > 0 ? "\uE838" : "\uE8B7";

    /// <inheritdoc/>
    public override bool ValidateItemName(string name) => InputValidation.IsValidFileName(name);

    /// <inheritdoc/>
    protected override int DoGetChildrenCount()
    {
        Debug.Fail("should never be called");

        // The entire project tree should be expanded when the view is loaded. As a result, the Children.Count will be
        // used and this method should not be called. As a precautionary measure, we'll simply return 1, indicating that
        // the item may have children and can be expanded. Once expanded, the real count of the children will be
        // updated.
        return 1;
    }

    /// <inheritdoc/>
    protected override async Task LoadChildren()
    {
        await foreach (var child in this.folder.GetFoldersAsync().ConfigureAwait(true).ConfigureAwait(false).ConfigureAwait(false))
        {
            try
            {
                this.AddChildInternal(
                    new FolderTreeItemAdapter(this.logger, this.contentBrowserState, child, child.Name)
                    {
                        // IMPORTANT: The entire tree should be expanded to avoid calls to GetChildrenCount and to be able
                        // To mark selected items specified in the ActiveRoute query params
                        IsExpanded = true,
                    });
            }
#pragma warning disable CA1031 // exceptions will not stop the loading of the rest of the folders
            catch (Exception ex)
            {
                // Log the failure, but continue with the rest
                this.CouldNotLoadProjectFolders(this.folder.Location, ex.Message);
            }
#pragma warning restore CA1031
        }
    }

    /// <inheritdoc/>
    protected override void OnPropertyChanged(PropertyChangedEventArgs e)
    {
        base.OnPropertyChanged(e);
        if (e.PropertyName?.Equals(nameof(this.IsExpanded), StringComparison.Ordinal) == true)
        {
            this.OnPropertyChanged(nameof(this.IconGlyph));
        }

        if (e.PropertyName?.Equals(nameof(this.IsSelected), StringComparison.Ordinal) == true)
        {
            if (this.IsSelected)
            {
                this.contentBrowserState.AddSelectedFolder(this.folder);
            }
            else
            {
                this.contentBrowserState.RemoveSelectedFolder(this.folder);
            }
        }
    }

    private void RestoreFromContentBrowserState()
        => this.IsSelected = this.contentBrowserState.ContainsSelectedFolder(this.folder);

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "An error occurred while loading project folders from `{location}`: {error}")]
    private partial void CouldNotLoadProjectFolders(string location, string error);
}
