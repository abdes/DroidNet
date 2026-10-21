// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Numerics;
using System.Runtime.CompilerServices;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Utils;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Environment;

/// <summary>Owns source roles, source observation, canonical light editors and initial-state reset snapshots.</summary>
public sealed partial class AtmosphereLightsSectionViewModel : ObservableObject, IDisposable
{
    private const string ResetLabel = "Reset Atmosphere Lights";
    private readonly InspectorEditSessionCoordinator? lightAssignments;
    private readonly Func<SceneAuthoring?>? authoringProvider;
    private readonly Func<NodeId, Task>? inspectSceneNode;
    private readonly ConditionalWeakTable<Scene, Dictionary<NodeId, PropertyEdit>> initialAtmosphereSources = new();
    private Scene? scene;
    private bool isApplyingEditorValues;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="AtmosphereLightsSectionViewModel"/> class.</summary>
    /// <param name="owner">The borrowed scene lifetime and feedback.</param>
    /// <param name="authoring">The authoring of the scene being inspected.</param>
    /// <param name="inspect">The existing hierarchy navigation operation.</param>
    internal AtmosphereLightsSectionViewModel(
        SceneEnvironmentEditOwner owner,
        Func<SceneAuthoring?>? authoring,
        Func<NodeId, Task>? inspect)
    {
        this.EditOwner = owner;
        this.authoringProvider = authoring;
        this.inspectSceneNode = inspect;
        this.PrimaryAtmosphereSource = new(authoring);
        this.SecondaryAtmosphereSource = new(authoring);
        if (authoring is not null)
        {
            this.lightAssignments = new(authoring, "Assign Atmosphere Light", this.RefreshSunDependencies, environment: false, owner.Diagnostics);
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
    public InspectorFieldDiagnostic SunReferenceDiagnostic => this.EditOwner.Diagnostics.Get(SceneProperties.DirectionalLight.AtmosphereSlot.Id);

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

    /// <summary>Restores initial roles and source settings as one edit.</summary>
    /// <returns>The reset completion.</returns>
    public async Task ResetAtmosphereSourcesAsync()
    {
        if (!this.EditOwner.IsInputEnabled || this.scene is not { } current
            || this.authoringProvider?.Invoke() is not { } authoring || !ReferenceEquals(authoring.Scene, current))
        {
            return;
        }

        var initial = this.initialAtmosphereSources.GetValue(current, CaptureAtmosphereSources);
        var edits = new Dictionary<NodeId, PropertyEdit>();
        foreach (var node in current.AllNodes)
        {
            if (node.Components.OfType<DirectionalLightComponent>().FirstOrDefault() is not { } light)
            {
                continue;
            }

            if (initial.TryGetValue(node.Id, out var original))
            {
                edits.Add(node.Id, original);
            }
            else if (light.AtmosphereSlot != AtmosphereLightSlot.None)
            {
                edits.Add(node.Id, PropertyEdit.SingleEdit(SceneProperties.DirectionalLight.AtmosphereSlot, AtmosphereLightSlot.None));
            }
        }

        var rotationMask = new PropertyEdit.Builder()
            .Set(SceneProperties.Transform.RotationX, 0f)
            .Set(SceneProperties.Transform.RotationY, 0f)
            .Set(SceneProperties.Transform.RotationZ, 0f)
            .Build();
        var lightMask = new PropertyEdit.Builder()
            .Set(SceneProperties.DirectionalLight.AtmosphereSlot, AtmosphereLightSlot.None)
            .Set(SceneProperties.DirectionalLight.AngularSizeRadians, 0f)
            .Set(SceneProperties.DirectionalLight.UsePerPixelAtmosphereTransmittance, value: false)
            .Set(SceneProperties.DirectionalLight.AtmosphereDiskLuminanceScaleRgb, Vector3.One)
            .Build();
        var rotations = edits.Where(pair => pair.Value.Contains(SceneProperties.Transform.RotationX.Id))
            .ToDictionary(pair => pair.Key, pair => pair.Value.IntersectIds(rotationMask));
        List<ISceneEdit> steps = [new SetProperties(edits.ToDictionary(pair => pair.Key, pair => pair.Value.IntersectIds(lightMask)), ResetLabel)];
        if (rotations.Count > 0)
        {
            steps.Add(new SetProperties(rotations, ResetLabel));
        }

        _ = await authoring.ExecuteAsync(new SceneEditBatch(ResetLabel, steps)).ConfigureAwait(true);
        this.RefreshSunDependencies();
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

    private static Dictionary<NodeId, PropertyEdit> CaptureAtmosphereSources(Scene scene)
    {
        var result = new Dictionary<NodeId, PropertyEdit>();
        foreach (var node in scene.AllNodes)
        {
            if (node.Components.OfType<DirectionalLightComponent>().FirstOrDefault() is not { AtmosphereSlot: not AtmosphereLightSlot.None } light)
            {
                continue;
            }

            var rotation = TransformConverter.QuaternionToEulerDegrees(node.Components.OfType<TransformComponent>().Single().LocalRotation);
            result.Add(node.Id, new PropertyEdit.Builder()
                .Set(SceneProperties.DirectionalLight.AtmosphereSlot, light.AtmosphereSlot)
                .Set(SceneProperties.DirectionalLight.AngularSizeRadians, light.AngularSizeRadians)
                .Set(SceneProperties.DirectionalLight.UsePerPixelAtmosphereTransmittance, light.UsePerPixelAtmosphereTransmittance)
                .Set(SceneProperties.DirectionalLight.AtmosphereDiskLuminanceScaleRgb, light.AtmosphereDiskLuminanceScaleRgb)
                .Set(SceneProperties.Transform.RotationX, rotation.X)
                .Set(SceneProperties.Transform.RotationY, rotation.Y)
                .Set(SceneProperties.Transform.RotationZ, rotation.Z)
                .Build());
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

    private void ApplyAtmosphereAssignment(AtmosphereLightSlot slot, NodeId? selected)
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

        var edits = new Dictionary<NodeId, PropertyEdit>();
        if (current is not null)
        {
            edits[current.Id] = PropertyEdit.SingleEdit(SceneProperties.DirectionalLight.AtmosphereSlot, AtmosphereLightSlot.None);
        }

        if (selected is { } selectedId)
        {
            edits[selectedId] = PropertyEdit.SingleEdit(SceneProperties.DirectionalLight.AtmosphereSlot, slot);
        }

        if (edits.Count > 0)
        {
            this.lightAssignments?.Bind(edits.Keys.ToArray());
            this.lightAssignments?.Submit(edits);
        }
    }

    private void RebuildSunOptions(NodeId? primaryNodeId, NodeId? secondaryNodeId)
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
