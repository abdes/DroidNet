// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.WorldEditor.Controls;
using Oxygen.Editor.WorldEditor.Editors.Scene;

namespace Oxygen.Editor.WorldEditor.Editors;

/// <summary>
/// ViewModel for the Scene Editor.
/// </summary>
public partial class SceneEditorViewModel : ObservableObject
{
    private readonly ILoggerFactory? loggerFactory;
    private SceneViewLayout? previousLayout;

    /// <summary>
    /// Initializes a new instance of the <see cref="SceneEditorViewModel"/> class.
    /// </summary>
    /// <param name="metadata">The scene document metadata.</param>
    /// <param name="loggerFactory">The logger factory.</param>
    public SceneEditorViewModel(SceneDocumentMetadata metadata, ILoggerFactory? loggerFactory = null)
    {
        this.Metadata = metadata;
        this.loggerFactory = loggerFactory;
        this.Viewports = [];
        this.CurrentLayout = SceneViewLayout.OnePane;
        this.UpdateLayout();
    }

    /// <summary>
    /// Gets or sets the scene document metadata.
    /// </summary>
    [ObservableProperty]
    public partial SceneDocumentMetadata Metadata { get; set; }

    /// <summary>
    /// Gets or sets the current layout of the viewports.
    /// </summary>
    [ObservableProperty]
    public partial SceneViewLayout CurrentLayout { get; set; }

    /// <summary>
    /// Gets the collection of active viewports.
    /// </summary>
    public ObservableCollection<ViewportViewModel> Viewports { get; }

    partial void OnCurrentLayoutChanged(SceneViewLayout value)
    {
        this.UpdateLayout();
    }

    private void UpdateLayout()
    {
        var (rows, cols) = SceneLayoutHelpers.GetGridDimensions(this.CurrentLayout);
        var requiredCount = rows * cols;

        // Adjust viewports count
        while (this.Viewports.Count < requiredCount)
        {
            var viewport = new ViewportViewModel(this.loggerFactory);
            viewport.ToggleMaximizeCommand = new RelayCommand(() => this.ToggleMaximize(viewport));
            this.Viewports.Add(viewport);
        }

        while (this.Viewports.Count > requiredCount)
        {
            this.Viewports.RemoveAt(this.Viewports.Count - 1);
        }

        // Update IsMaximized state for all viewports
        foreach (var viewport in this.Viewports)
        {
            viewport.IsMaximized = this.CurrentLayout == SceneViewLayout.OnePane && this.previousLayout != null;
        }
    }

    private void ToggleMaximize(ViewportViewModel viewport)
    {
        if (this.CurrentLayout == SceneViewLayout.OnePane)
        {
            // Restore
            if (this.previousLayout != null)
            {
                this.CurrentLayout = this.previousLayout.Value;
                this.previousLayout = null;
            }
        }
        else
        {
            // Maximize
            this.previousLayout = this.CurrentLayout;
            this.CurrentLayout = SceneViewLayout.OnePane;

            // Move the maximized viewport to the first position so it stays visible
            var index = this.Viewports.IndexOf(viewport);
            if (index > 0)
            {
                this.Viewports.Move(index, 0);
            }
        }
    }
}
