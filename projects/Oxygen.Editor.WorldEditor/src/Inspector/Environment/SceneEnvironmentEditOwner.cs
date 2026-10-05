// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Controls;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector.Environment;

/// <summary>Owns the single scene-environment edit lifetime borrowed by all section models.</summary>
public sealed class SceneEnvironmentEditOwner : ObservableObject, IInspectorEditSessionOwner, IDisposable
{
    private readonly InspectorEditSessionCoordinator? coordinator;
    private bool isInputEnabled = true;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="SceneEnvironmentEditOwner"/> class.</summary>
    /// <param name="commands">The existing scene command service.</param>
    /// <param name="context">The current document context provider.</param>
    /// <param name="refresh">The owning parent's scene refresh.</param>
    /// <param name="diagnostics">The canonical scene feedback.</param>
    internal SceneEnvironmentEditOwner(
        ISceneDocumentCommandService? commands,
        Func<SceneDocumentCommandContext?>? context,
        Action refresh,
        InspectorFieldDiagnostics diagnostics)
    {
        this.Diagnostics = diagnostics;
        if (commands is not null && context is not null)
        {
            this.coordinator = new(commands, context, "Edit Environment", refresh, environment: true, diagnostics);
            diagnostics.Relate(SceneDocumentCommandService.SceneEnvironment.AutoExposureMinEv.Id, SceneDocumentCommandService.SceneEnvironment.AutoExposureMaxEv.Id);
            diagnostics.Relate(SceneDocumentCommandService.SceneEnvironment.AutoExposureLowPercentile.Id, SceneDocumentCommandService.SceneEnvironment.AutoExposureHighPercentile.Id);
        }
    }

    /// <inheritdoc />
    public Guid EditScopeId => this.coordinator?.ScopeId ?? Guid.Empty;

    /// <summary>Gets a value indicating whether the bound scene accepts authoring input.</summary>
    public bool IsInputEnabled => this.isInputEnabled && !this.disposed;

    /// <summary>Gets the currently bound scene identity.</summary>
    internal Scene? Scene { get; private set; }

    /// <summary>Gets feedback shared by all ordinary sections.</summary>
    internal InspectorFieldDiagnostics Diagnostics { get; }

    /// <summary>Gets a value indicating whether authored state is being displayed.</summary>
    internal bool IsRefreshing { get; private set; }

    /// <summary>Gets completion of submitted scene edits.</summary>
    internal Task Pending => this.coordinator?.Pending ?? Task.CompletedTask;

    /// <inheritdoc />
    public void BeginEditSession(string field, NumberBoxEditInteractionKind interaction)
        => this.coordinator?.Begin(field, interaction);

    /// <inheritdoc />
    public void CompleteEditSession(NumberBoxEditSessionEventArgs args) => this.coordinator?.Complete(args);

    /// <inheritdoc />
    public void EndEditSession(NumberBoxEditCompletionKind completion) => this.coordinator?.End(completion);

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.coordinator?.Dispose();
        this.Scene = null;
    }

    /// <summary>Changes scene identity through the existing coordinator's lifetime guard.</summary>
    /// <param name="scene">The new scene or null.</param>
    internal void Bind(Scene? scene)
    {
        this.coordinator?.Bind(scene is null ? [] : [scene.Id]);
        this.Scene = scene;
    }

    /// <summary>Settles hidden gestures using existing coordinator policy.</summary>
    /// <param name="enabled">Whether input is active.</param>
    internal void SetInputEnabled(bool enabled)
    {
        _ = this.SetProperty(ref this.isInputEnabled, enabled, nameof(this.IsInputEnabled));
        this.coordinator?.SetInputEnabled(enabled);
    }

    /// <summary>Guards reentrant display refresh without submitting authoring changes.</summary>
    /// <param name="refresh">The display update.</param>
    internal void Refresh(Action refresh)
    {
        var previous = this.IsRefreshing;
        this.IsRefreshing = true;
        try
        {
            refresh();
        }
        finally
        {
            this.IsRefreshing = previous;
        }
    }

    /// <summary>Routes a section value to canonical model-change or authoring semantics.</summary>
    /// <typeparam name="T">The authored value type.</typeparam>
    /// <param name="property">The canonical property identity.</param>
    /// <param name="value">The authored value.</param>
    internal void Apply<T>(PropertyId<T> property, T value)
    {
        if (this.IsRefreshing)
        {
            this.coordinator?.ModelChanged(property.Id);
        }
        else if (this.Scene is not null && this.IsInputEnabled)
        {
            this.coordinator?.Submit(PropertyEdit.SingleEdit(property, value));
        }
    }
}
