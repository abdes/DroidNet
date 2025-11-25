// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics.CodeAnalysis;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Aura.Settings;
using DroidNet.Config;
using DryIoc;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.WorldEditor.Controls;
using Oxygen.Editor.WorldEditor.Engine;
using Oxygen.Editor.Documents;

namespace Oxygen.Editor.WorldEditor.Editors.Scene;

/// <summary>
/// ViewModel for the Scene Editor.
/// </summary>
public partial class SceneEditorViewModel : ObservableObject
{
    private readonly ILoggerFactory? loggerFactory;
    private readonly IEngineService engineService;
    private readonly IContainer container;
    private SceneViewLayout? previousLayout;

    /// <summary>
    /// Initializes a new instance of the <see cref="SceneEditorViewModel"/> class.
    /// </summary>
    /// <param name="metadata">The scene document metadata.</param>
    /// <param name="engineService">Coordinates native engine usage for the document.</param>
    /// <param name="loggerFactory">The logger factory.</param>
    public SceneEditorViewModel(SceneDocumentMetadata metadata, IEngineService engineService, IContainer container, ILoggerFactory? loggerFactory = null)
    {
        this.engineService = engineService;
        this.loggerFactory = loggerFactory;
        this.Viewports = new ObservableCollection<ViewportViewModel>();
        this.Metadata = metadata;
        this.container = container;

        // Try to restore layout from metadata if present
        this.CurrentLayout = SceneViewLayout.OnePane;

        this.ChangeLayoutCommand = new RelayCommand<SceneViewLayout>(layout => this.CurrentLayout = layout);
        this.UpdateLayout(this.CurrentLayout);
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

    /// <summary>
    /// Gets the command that changes the current layout.
    /// </summary>
    public RelayCommand<SceneViewLayout> ChangeLayoutCommand { get; private set; } = null!;

    partial void OnCurrentLayoutChanging(SceneViewLayout value)
    {
        this.UpdateLayout(value);
    }

    private void UpdateLayout(SceneViewLayout targetLayout)
    {
        var metadata = this.Metadata ?? throw new InvalidOperationException("Scene metadata is not initialized.");
        metadata.Layout = targetLayout;

        var placements = SceneLayoutHelpers.GetPlacements(targetLayout);
        var requiredCount = placements.Count;

        // Adjust viewports count
        while (this.Viewports.Count < requiredCount)
        {
            var settings = this.container.Resolve<ISettingsService<IAppearanceSettings>>();
            var viewport = new ViewportViewModel(metadata.DocumentId, this.engineService, settings, this.loggerFactory);
            viewport.ToggleMaximizeCommand = new RelayCommand(() => this.ToggleMaximize(viewport));
            viewport.OnLayoutRequested = requestedLayout => this.ChangeLayoutCommand.Execute(requestedLayout);
            this.Viewports.Add(viewport);
        }

        while (this.Viewports.Count > requiredCount)
        {
            this.Viewports.RemoveAt(this.Viewports.Count - 1);
        }

        // Update IsMaximized state and metadata for all viewports
        for (var i = 0; i < this.Viewports.Count; i++)
        {
            var viewport = this.Viewports[i];
            viewport.IsMaximized = targetLayout == SceneViewLayout.OnePane && this.previousLayout != null;

            // The first viewport is considered the main camera
            viewport.UpdateLayoutMetadata(i, i == 0);
            viewport.OnLayoutRequested = requestedLayout => this.ChangeLayoutCommand.Execute(requestedLayout);
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
