// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Owns component filtering without changing authored selection or properties.</summary>
public sealed partial class SceneNodeEditorViewModel
{
    private readonly HashSet<ComponentPropertyEditor> boundEditors = [];
    private SceneNode[] filterNodes = [];
    private Scene? filterScene;
    private Guid? filterDocument;
    private Type? selectedComponentType;

    /// <summary>Occurs before changing sections so the view can complete pending text input.</summary>
    public event EventHandler? ComponentFilterChanging;

    /// <summary>Gets the component types represented by the current selection.</summary>
    public ObservableCollection<InspectorComponentFilter> ComponentFilters { get; } = [];

    /// <summary>Gets the active component filter, or null for all applicable property sections.</summary>
    public Type? SelectedComponentType => this.selectedComponentType;

    /// <summary>Gets the selected component instance for single-node component actions.</summary>
    public GameComponent? SelectedComponent => this.SelectedNode?.Components.FirstOrDefault(component => component.GetType() == this.selectedComponentType);

    /// <summary>Gets a value indicating whether all applicable property sections are visible.</summary>
    public bool IsAllComponentsSelected => this.selectedComponentType is null;

    /// <summary>Gets a value indicating whether the selected component has no property editor.</summary>
    public bool HasUnavailableComponentEditor => this.selectedComponentType is { } type && !this.propertyEditorFactories.ContainsKey(type);

    /// <summary>Gets the explanation shown instead of an empty property pane.</summary>
    public string UnavailableComponentEditorMessage => this.HasUnavailableComponentEditor
        ? $"A property editor is not available for {ComponentLabel(this.selectedComponentType!)} yet." : string.Empty;

    /// <summary>Selects a component type, toggling an already selected type back to All.</summary>
    /// <param name="componentType">The component type, or null to show all components.</param>
    public void SelectComponentFilter(Type? componentType)
    {
        if (this.isDisposed || (componentType is not null
            && !this.ComponentFilters.Any(option => option.ComponentType == componentType && option.IsAvailable)))
        {
            return;
        }

        var nextType = this.selectedComponentType == componentType ? null : componentType;
        if (nextType != this.selectedComponentType)
        {
            this.ComponentFilterChanging?.Invoke(this, EventArgs.Empty);
        }

        this.SetComponentFilter(nextType);
        this.RefreshPropertyEditors(refreshValues: false);
        this.RefreshEditorInputState();
    }

    private static string ComponentLabel(Type type)
        => type.Name switch
        {
            nameof(TransformComponent) => "Transform",
            nameof(GeometryComponent) => "Geometry",
            nameof(PerspectiveCamera) => "Camera",
            nameof(OrthographicCamera) => "Orthographic Camera",
            nameof(DirectionalLightComponent) => "Directional Light",
            nameof(PointLightComponent) => "Point Light",
            nameof(SpotLightComponent) => "Spot Light",
            _ => type.Name,
        };

    private void RefreshEditorInputState()
    {
        foreach (var editor in this.editorInstances.Values.Cast<ComponentPropertyEditor>().ToArray())
        {
            if (this.boundEditors.Add(editor))
            {
                editor.UpdateValues(this.items.ToArray());
            }

            editor.SetInputEnabled(this.PropertyEditors.Contains(editor));
        }
    }

    private void SetComponentFilter(Type? componentType)
    {
        var changed = this.selectedComponentType != componentType;
        this.selectedComponentType = componentType;
        foreach (var option in this.ComponentFilters)
        {
            option.IsSelected = option.ComponentType == componentType;
        }

        this.OnPropertyChanged(nameof(this.SelectedComponent));
        if (changed)
        {
            this.OnPropertyChanged(nameof(this.SelectedComponentType));
            this.OnPropertyChanged(nameof(this.IsAllComponentsSelected));
            this.OnPropertyChanged(nameof(this.HasUnavailableComponentEditor));
            this.OnPropertyChanged(nameof(this.UnavailableComponentEditorMessage));
        }
    }

    private void ReconcileComponentFilters()
    {
        var document = this.documentService.GetActiveDocumentId(this.windowId);
        var selectedType = this.selectedComponentType;
        if (!ReferenceEquals(this.filterScene, this.activeScene) || this.filterDocument != document
            || this.filterNodes.Length != this.items.Count
            || this.filterNodes.Any(previous => !this.items.Any(current => ReferenceEquals(previous, current))))
        {
            this.filterNodes = this.items.ToArray();
            this.filterScene = this.activeScene;
            this.filterDocument = document;
            selectedType = null;
        }

        var present = this.items.SelectMany(static node => node.Components).Select(static component => component.GetType()).ToHashSet();
        for (var index = this.ComponentFilters.Count - 1; index >= 0; --index)
        {
            if (!present.Contains(this.ComponentFilters[index].ComponentType))
            {
                this.ComponentFilters.RemoveAt(index);
            }
        }

        foreach (var type in present.OrderBy(type => type == typeof(TransformComponent) ? 0 : 1).ThenBy(ComponentLabel, StringComparer.Ordinal))
        {
            var option = this.ComponentFilters.FirstOrDefault(candidate => candidate.ComponentType == type);
            if (option is null)
            {
                option = new(type, ComponentLabel(type));
                this.ComponentFilters.Add(option);
            }

            var presentCount = this.items.Count(node => node.Components.Any(component => component.GetType() == type));
            option.IsAvailable = presentCount == this.items.Count;
            option.UnavailableReason = !option.IsAvailable ? "This component is not present on every selected node."
                : !this.propertyEditorFactories.ContainsKey(type) ? "A property editor is not available for this component yet." : string.Empty;
            option.StatusLabel = !option.IsAvailable ? string.Create(System.Globalization.CultureInfo.CurrentCulture, $"{presentCount}/{this.items.Count}")
                : !this.propertyEditorFactories.ContainsKey(type) ? "No editor" : string.Empty;
        }

        this.SetComponentFilter(this.ComponentFilters.Any(option => option.ComponentType == selectedType && option.IsAvailable) ? selectedType : null);
    }
}
