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

public partial class FolderTreeItemAdapter : TreeItemAdapter
{
    private readonly ILogger logger;
    private readonly Task<IFolder> folderAsync;
    private string label;

    /// <summary>
    /// Initializes a new instance of the <see cref="FolderTreeItemAdapter"/> class.
    /// </summary>
    /// <param name="logger"></param>
    /// <param name="folderAsync"></param>
    /// <param name="label"></param>
    /// <param name="isRoot"></param>
    /// <param name="isHidden"></param>
    public FolderTreeItemAdapter(
        ILogger logger,
        Task<IFolder> folderAsync,
        string label,
        bool isRoot = false,
        bool isHidden = false)
        : base(isRoot, isHidden)
    {
        this.logger = logger;
        this.folderAsync = folderAsync;
        this.label = label;

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
        var folder = await this.folderAsync.ConfigureAwait(true);
        await foreach (var child in folder.GetFoldersAsync().ConfigureAwait(true))
        {
#pragma warning disable CA1031 // Do not catch general exception types
            try
            {
                this.AddChildInternal(
                    new FolderTreeItemAdapter(this.logger, Task.FromResult(child), child.Name)
                    {
                        // IMPORTANT: The entire tree should be expanded to avoid calls to GetChildrenCount and to be able
                        // To mark selected items specified in the ActiveRoute query params
                        IsExpanded = true,
                    });
            }
            catch (Exception ex)
            {
                // Log the failure, but continue with the rest
                CouldNotLoadProjectFolders(this.logger, folder.Location, ex.Message);
            }
#pragma warning restore CA1031 // Do not catch general exception types
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
    }

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "An error occurred while loading project folders from `{location}`: {error}")]
    private static partial void CouldNotLoadProjectFolders(ILogger logger, string location, string error);
}
