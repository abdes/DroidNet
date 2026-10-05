// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Numerics;
using System.Runtime.CompilerServices;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Utils;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector.Environment;

/// <summary>Owns source roles, source observation, canonical light editors and initial-state reset snapshots.</summary>
public sealed partial class AtmosphereLightsSectionViewModel : ObservableObject, IDisposable
{
    private readonly InspectorEditSessionCoordinator? lightAssignments;
    private readonly Func<SceneDocumentCommandContext?>? commandContextProvider;
    private readonly Func<Guid, Task>? inspectSceneNode;
    private readonly ConditionalWeakTable<Scene, Dictionary<Guid, PropertyEdit>> initialAtmosphereSources = new();
    private Scene? scene;
    private bool isApplyingEditorValues;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="AtmosphereLightsSectionViewModel"/> class.</summary>
    /// <param name="owner">The borrowed scene lifetime and feedback.</param>
    /// <param name="commands">The existing light command service.</param>
    /// <param name="context">The current document context provider.</param>
    /// <param name="inspect">The existing hierarchy navigation operation.</param>
    internal AtmosphereLightsSectionViewModel(
        SceneEnvironmentEditOwner owner,
        ISceneDocumentCommandService? commands,
        Func<SceneDocumentCommandContext?>? context,
        Func<Guid, Task>? inspect)
    {
        this.EditOwner = owner;
        this.commandContextProvider = context;
        this.inspectSceneNode = inspect;
        this.PrimaryAtmosphereSource = new(commands, context);
        this.SecondaryAtmosphereSource = new(commands, context);
        if (commands is not null && context is not null)
        {
            this.lightAssignments = new(commands, context, "Assign Atmosphere Light", this.RefreshSunDependencies, environment: false, owner.Diagnostics);
        }
    }

    [ObservableProperty]
    public partial SunLightOption? SelectedSun { get; set; }

    [ObservableProperty]
    public partial SunLightOption? SelectedSecondarySun { get; set; }

    /// <summary>Gets the borrowed scene lifetime and diagnostic owner.</summary>
    public SceneEnvironmentEditOwner EditOwner { get; }

    /// <summary>Gets the stable primary light editor owned by this section.</summary>
    public DirectionalLightViewModel PrimaryAtmosphereSource { get; }

    /// <summary>Gets the stable secondary light editor owned by this section.</summary>
    public DirectionalLightViewModel SecondaryAtmosphereSource { get; }

    /// <summary>Gets primary source choices.</summary>
    public ObservableCollection<SunLightOption> SunOptions { get; } = [];

    /// <summary>Gets secondary source choices.</summary>
    public ObservableCollection<SunLightOption> SecondarySunOptions { get; } = [];

    /// <summary>Gets a value indicating whether the primary source is inspectable.</summary>
    public bool HasPrimaryAtmosphereSource => this.SelectedSun?.NodeId is not null;

    /// <summary>Gets a value indicating whether the secondary source is inspectable.</summary>
    public bool HasSecondaryAtmosphereSource => this.SelectedSecondarySun?.NodeId is not null;

    /// <summary>Gets the shared source-assignment feedback.</summary>
    public InspectorFieldDiagnostic SunReferenceDiagnostic => this.EditOwner.Diagnostics.Get(SceneDocumentCommandService.DirectionalLight.AtmosphereSlot.Id);

    /// <summary>Gets completion of source-role edits.</summary>
    internal Task Pending => this.lightAssignments?.Pending ?? Task.CompletedTask;

    /// <summary>Reveals a source in the existing hierarchy without authoring data.</summary>
    /// <param name="source">The requested current source.</param>
    /// <returns>The navigation completion.</returns>
    public Task InspectAtmosphereSourceAsync(SunLightOption? source)
        => source?.NodeId is { } nodeId && this.inspectSceneNode is { } inspect ? inspect(nodeId) : Task.CompletedTask;

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.DetachSceneObservers();
        this.lightAssignments?.Dispose();
        this.PrimaryAtmosphereSource.Dispose();
        this.SecondaryAtmosphereSource.Dispose();
    }

    /// <summary>Restores initial roles and source settings as one history entry.</summary>
    /// <returns>The reset completion.</returns>
    public async Task ResetAtmosphereSourcesAsync()
    {
        if (!this.EditOwner.IsInputEnabled || this.scene is not { } current
            || this.lightAssignments is not { } assignments || this.commandContextProvider?.Invoke() is not { } context)
        {
            return;
        }

        var initial = this.initialAtmosphereSources.GetValue(current, CaptureAtmosphereSources);
        var edits = new Dictionary<Guid, PropertyEdit>();
        foreach (var node in current.AllNodes)
        {
            if (node.Components.OfType<DirectionalLightComponent>().FirstOrDefault() is not { } light)
            {
                continue;
            }

            if (initial.TryGetValue(node.Id, out var original))
            {
                edits.Add(node.Id, original.Clone());
            }
            else if (light.AtmosphereSlot != AtmosphereLightSlot.None)
            {
                edits.Add(node.Id, PropertyEdit.SingleEdit(SceneDocumentCommandService.DirectionalLight.AtmosphereSlot, AtmosphereLightSlot.None));
            }
        }

        var rotationMask = PropertyEdit.SingleEdit(SceneDocumentCommandService.Transform.RotationX, 0f);
        rotationMask.Set(SceneDocumentCommandService.Transform.RotationY, 0f);
        rotationMask.Set(SceneDocumentCommandService.Transform.RotationZ, 0f);
        var lightMask = PropertyEdit.SingleEdit(SceneDocumentCommandService.DirectionalLight.AtmosphereSlot, AtmosphereLightSlot.None);
        lightMask.Set(SceneDocumentCommandService.DirectionalLight.AngularSizeRadians, 0f);
        lightMask.Set(SceneDocumentCommandService.DirectionalLight.UsePerPixelAtmosphereTransmittance, value: false);
        lightMask.Set(SceneDocumentCommandService.DirectionalLight.AtmosphereDiskLuminanceScaleRgb, Vector3.One);
        context.History.BeginChangeSet("Reset Atmosphere Lights");
        try
        {
            assignments.Bind(edits.Keys.ToArray());
            assignments.Submit(edits.ToDictionary(pair => pair.Key, pair => pair.Value.IntersectIds(lightMask)));
            await assignments.Pending.ConfigureAwait(true);
            if (!string.IsNullOrEmpty(this.SunReferenceDiagnostic.Message) || !ReferenceEquals(this.scene, current)
                || !this.EditOwner.IsInputEnabled || this.disposed)
            {
                return;
            }

            var rotations = edits.Where(pair => pair.Value.Contains(SceneDocumentCommandService.Transform.RotationX.Id))
                .ToDictionary(pair => pair.Key, pair => pair.Value.IntersectIds(rotationMask));
            if (rotations.Count > 0)
            {
                assignments.Bind(rotations.Keys.ToArray());
                assignments.Submit(rotations);
                await assignments.Pending.ConfigureAwait(true);
            }
        }
        finally
        {
            context.History.EndChangeSet();
        }
    }

    /// <summary>Rebinds source observation and preserves per-scene initial snapshots.</summary>
    /// <param name="value">The new scene or null.</param>
    internal void Bind(Scene? value)
    {
        this.DetachSceneObservers();
        this.lightAssignments?.Bind([]);
        this.scene = value;
        if (value is not null)
        {
            _ = this.initialAtmosphereSources.GetValue(value, CaptureAtmosphereSources);
        }

        this.AttachSceneObservers();
        this.RefreshSunDependencies();
    }

    /// <summary>Applies the parent's input gate to role and canonical source editors.</summary>
    /// <param name="enabled">Whether authoring input is active.</param>
    internal void SetInputEnabled(bool enabled)
    {
        this.lightAssignments?.SetInputEnabled(enabled);
        this.PrimaryAtmosphereSource.SetInputEnabled(enabled);
        this.SecondaryAtmosphereSource.SetInputEnabled(enabled);
    }

    private static Dictionary<Guid, PropertyEdit> CaptureAtmosphereSources(Scene scene)
    {
        var result = new Dictionary<Guid, PropertyEdit>();
        foreach (var node in scene.AllNodes)
        {
            if (node.Components.OfType<DirectionalLightComponent>().FirstOrDefault() is not { AtmosphereSlot: not AtmosphereLightSlot.None } light)
            {
                continue;
            }

            var edit = PropertyEdit.SingleEdit(SceneDocumentCommandService.DirectionalLight.AtmosphereSlot, light.AtmosphereSlot);
            edit.Set(SceneDocumentCommandService.DirectionalLight.AngularSizeRadians, light.AngularSizeRadians);
            edit.Set(SceneDocumentCommandService.DirectionalLight.UsePerPixelAtmosphereTransmittance, light.UsePerPixelAtmosphereTransmittance);
            edit.Set(SceneDocumentCommandService.DirectionalLight.AtmosphereDiskLuminanceScaleRgb, light.AtmosphereDiskLuminanceScaleRgb);
            var rotation = TransformConverter.QuaternionToEulerDegrees(node.Components.OfType<TransformComponent>().Single().LocalRotation);
            edit.Set(SceneDocumentCommandService.Transform.RotationX, rotation.X);
            edit.Set(SceneDocumentCommandService.Transform.RotationY, rotation.Y);
            edit.Set(SceneDocumentCommandService.Transform.RotationZ, rotation.Z);
            result.Add(node.Id, edit);
        }

        return result;
    }

    [RelayCommand]
    private void ClearSun() => this.ClearSource(AtmosphereLightSlot.Primary);

    [RelayCommand]
    private void ClearSecondarySun() => this.ClearSource(AtmosphereLightSlot.Secondary);

    private void ClearSource(AtmosphereLightSlot slot)
    {
        this.isApplyingEditorValues = true;
        try
        {
            if (slot == AtmosphereLightSlot.Primary)
            {
                this.SelectedSun = this.SunOptions.FirstOrDefault(static option => option.NodeId is null);
            }
            else
            {
                this.SelectedSecondarySun = this.SecondarySunOptions.FirstOrDefault(static option => option.NodeId is null);
            }
        }
        finally
        {
            this.isApplyingEditorValues = false;
        }

        this.ApplyAtmosphereAssignment(slot, selected: null);
    }

    partial void OnSelectedSunChanged(SunLightOption? value)
    {
        this.OnPropertyChanged(nameof(this.HasPrimaryAtmosphereSource));
        if (!this.isApplyingEditorValues && value is not null)
        {
            this.ApplyAtmosphereAssignment(AtmosphereLightSlot.Primary, value.NodeId);
        }
    }

    partial void OnSelectedSecondarySunChanged(SunLightOption? value)
    {
        this.OnPropertyChanged(nameof(this.HasSecondaryAtmosphereSource));
        if (!this.isApplyingEditorValues && value is not null)
        {
            this.ApplyAtmosphereAssignment(AtmosphereLightSlot.Secondary, value.NodeId);
        }
    }

    private SceneNode? FindAtmosphereSource(AtmosphereLightSlot slot)
        => this.scene?.AllNodes.FirstOrDefault(node => node.Components.OfType<DirectionalLightComponent>().Any(light => light.AtmosphereSlot == slot));

    private void ApplyAtmosphereAssignment(AtmosphereLightSlot slot, Guid? selected)
    {
        if (this.isApplyingEditorValues || !this.EditOwner.IsInputEnabled || this.scene is null || this.disposed)
        {
            return;
        }

        var current = this.FindAtmosphereSource(slot);
        if (current?.Id == selected)
        {
            return;
        }

        var edits = new Dictionary<Guid, PropertyEdit>();
        if (current is not null)
        {
            edits[current.Id] = PropertyEdit.SingleEdit(SceneDocumentCommandService.DirectionalLight.AtmosphereSlot, AtmosphereLightSlot.None);
        }

        if (selected is { } selectedId)
        {
            edits[selectedId] = PropertyEdit.SingleEdit(SceneDocumentCommandService.DirectionalLight.AtmosphereSlot, slot);
        }

        if (edits.Count > 0)
        {
            this.lightAssignments?.Bind(edits.Keys.ToArray());
            this.lightAssignments?.Submit(edits);
        }
    }

    private void RebuildSunOptions(Guid? primaryNodeId, Guid? secondaryNodeId)
    {
        this.SunOptions.Clear();
        this.SecondarySunOptions.Clear();
        var nonePrimary = new SunLightOption(NodeId: null, "None");
        var noneSecondary = new SunLightOption(NodeId: null, "None");
        this.SunOptions.Add(nonePrimary);
        this.SecondarySunOptions.Add(noneSecondary);
        this.SelectedSun = nonePrimary;
        this.SelectedSecondarySun = noneSecondary;
        foreach (var node in this.scene?.RootNodes.SelectMany(static root => SceneTraversal.CollectNodes(root)) ?? [])
        {
            if (node.Components.OfType<DirectionalLightComponent>().FirstOrDefault() is not { } light)
            {
                continue;
            }

            var primary = new SunLightOption(node.Id, light.AtmosphereSlot == AtmosphereLightSlot.Secondary ? $"{node.Name} · Secondary" : node.Name, light.AtmosphereSlot != AtmosphereLightSlot.Secondary);
            var secondary = new SunLightOption(node.Id, light.AtmosphereSlot == AtmosphereLightSlot.Primary ? $"{node.Name} · Primary" : node.Name, light.AtmosphereSlot != AtmosphereLightSlot.Primary);
            this.SunOptions.Add(primary);
            this.SecondarySunOptions.Add(secondary);
            if (primaryNodeId == node.Id)
            {
                this.SelectedSun = primary;
            }

            if (secondaryNodeId == node.Id)
            {
                this.SelectedSecondarySun = secondary;
            }
        }

        this.RefreshAtmosphereSourceEditors();
    }

    private void RefreshAtmosphereSourceEditors()
    {
        this.PrimaryAtmosphereSource.UpdateValues(this.FindAtmosphereSource(AtmosphereLightSlot.Primary) is { } primary ? [primary] : []);
        this.SecondaryAtmosphereSource.UpdateValues(this.FindAtmosphereSource(AtmosphereLightSlot.Secondary) is { } secondary ? [secondary] : []);
    }
}
