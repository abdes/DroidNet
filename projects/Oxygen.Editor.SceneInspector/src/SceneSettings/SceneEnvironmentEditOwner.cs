// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Controls;
using Oxygen.Editor.SceneInspector.Editing;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;

namespace Oxygen.Editor.SceneInspector.SceneSettings;

/// <summary>Owns the single scene-environment edit lifetime borrowed by all section models.</summary>
public sealed class SceneEnvironmentEditOwner : ObservableObject, IInspectorEditSessionOwner, IDisposable
{
    private readonly InspectorEditSessionCoordinator? coordinator;
    private bool isInputEnabled = true;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="SceneEnvironmentEditOwner"/> class.</summary>
    /// <param name="authoring">The authoring of the scene being inspected.</param>
    /// <param name="refresh">The owning parent's scene refresh.</param>
    /// <param name="diagnostics">The canonical scene feedback.</param>
    internal SceneEnvironmentEditOwner(
        Func<SceneAuthoring?>? authoring,
        Action refresh,
        InspectorFieldDiagnostics diagnostics)
    {
        this.Diagnostics = diagnostics;
        if (authoring is not null)
        {
            this.coordinator = new(authoring, "Edit Environment", refresh, environment: true, diagnostics);
            diagnostics.Relate(SceneProperties.SceneEnvironment.AutoExposureMinEv.Id, SceneProperties.SceneEnvironment.AutoExposureMaxEv.Id);
            diagnostics.Relate(SceneProperties.SceneEnvironment.AutoExposureLowPercentile.Id, SceneProperties.SceneEnvironment.AutoExposureHighPercentile.Id);
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
        // The environment targets the bound context's scene; rebinding follows the context.
        this.coordinator?.Bind([]);
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

    /// <summary>Routes a number edited in the inspector, which edits doubles, to a single-precision scene property.</summary>
    /// <param name="property">The canonical property identity.</param>
    /// <param name="value">The edited value; the scene stores it in single precision.</param>
    internal void Apply(PropertyId<float> property, double value) => this.Apply(property, (float)value);

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
