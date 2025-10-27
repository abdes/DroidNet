// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.ComponentModel;
using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.WorldEditor.PropertiesEditor;

/// <summary>
/// Represents the details of multiple selected items in the properties editor.
/// </summary>
/// <typeparam name="T">The type of the items being edited, which must be a <see cref="GameObject"/>.</typeparam>
/// <remarks>
/// This class provides the base functionality for handling multiple selected items in the properties editor.
/// It includes properties and methods for managing the collection of items, updating property editors, and
/// propagating changes to the items.
/// </remarks>
/// <remarks>
/// Base constructor that accepts an optional <see cref="ILoggerFactory"/> for constructor injection.
/// Derived view models should call this base constructor to initialize logging.
/// </remarks>
/// <param name="loggerFactory">Optional logger factory from DI.</param>
public abstract partial class MultiSelectionDetails<T>(ILoggerFactory? loggerFactory = null) : ObservableObject
    where T : GameObject
{
    private readonly ILogger logger = loggerFactory?.CreateLogger<MultiSelectionDetails<T>>() ?? NullLoggerFactory.Instance.CreateLogger<MultiSelectionDetails<T>>();
    private readonly ICollection<T> items = [];

    /// <summary>
    /// Flag indicating whether the rename operation should be propagated to the items. Set to true
    /// when the name change is originating from the item itself, for example when the item is
    /// renamed in the scene explorer tree.
    /// </summary>
    private bool doNotPropagateRename;

    /// <summary>
    /// Observable property bound to the editable text box used to rename the item(s).
    /// </summary>
    [ObservableProperty]
    private string? name;

    /// <summary>
    /// Gets the type of the items being edited. Can be used to show a custom icon or othe indication in the UI.
    /// </summary>
    public Type? ItemsType { get; } = typeof(T);

    /// <summary>
    /// Gets a value indicating whether there are any items at all.
    /// </summary>
    public bool HasItems => this.items.Count > 0;

    /// <summary>
    /// Gets a value indicating whether multiple items are being edited.
    /// </summary>
    public bool HasMultipleItems => this.items.Count > 1;

    /// <summary>
    /// Gets the number of items being edited.
    /// </summary>
    public int ItemsCount => this.items.Count;

    /// <summary>
    /// Gets an <see cref="ObservableCollection{T}"/> of <see cref="IPropertyEditor{T}"/> instances
    /// to be displayed in the UI for the items being edited. The content of the collection depends
    /// on the items being edited. Only property editors that are applicable to all the items will
    /// be included.
    /// </summary>
    public ObservableCollection<IPropertyEditor<T>> PropertyEditors { get; } = [];

    /// <summary>
    /// Filters the property editors to be displayed in the UI for the items being edited.
    /// </summary>
    /// <returns>
    /// A collection of <see cref="IPropertyEditor{T}"/> instances that are applicable to all the items being edited.
    /// </returns>
    /// <remarks>
    /// This method is called to determine which property editors should be shown based on the
    /// current selection of items. Derived classes should implement this method to provide the
    /// appropriate filtering logic.
    /// </remarks>
    protected abstract ICollection<IPropertyEditor<T>> FilterPropertyEditors();

    /// <summary>
    /// Resets the collection of items being edited.
    /// </summary>
    /// <param name="newCollection">The new collection of items being edited.</param>
    /// <remarks>
    /// This method is typically used when the items being edited change, for example when the user
    /// selects a different set of items somewhere else in the UI. This base class has no knowledge
    /// of the source of the items, so it is up to the derived class to provide the new collection.
    /// </remarks>>
    protected void UpdateItemsCollection(IEnumerable<T> newCollection)
    {
        foreach (var item in this.items)
        {
            item.PropertyChanged -= OnItemPropertyExternallyChanged;
        }

        this.items.Clear();

        foreach (var item in newCollection)
        {
            item.PropertyChanged += OnItemPropertyExternallyChanged;
            this.items.Add(item);
        }

        this.OnPropertyChanged(new PropertyChangedEventArgs(nameof(this.ItemsCount)));
        this.OnPropertyChanged(new PropertyChangedEventArgs(nameof(this.HasItems)));
        this.OnPropertyChanged(new PropertyChangedEventArgs(nameof(this.HasMultipleItems)));

    this.RefreshOwnProperties();
    this.UpdatePropertyEditors();
    this.UpdatePropertyEditorsValues();

    // Log the collection update
    this.LogItemsCollectionUpdated();
    return;

        void OnItemPropertyExternallyChanged(object? sender, PropertyChangedEventArgs e)
        {
            this.RefreshOwnProperties();
        }
    }

    /// <summary>
    /// Refreshes the properties of the view model that are based on the items being edited.
    /// </summary>
    /// <remarks>
    /// Derived classes, which provide additional properties based on the items being edited, should
    /// override this method to update those properties.
    /// </remarks>
    protected virtual void RefreshOwnProperties()
    {
    this.doNotPropagateRename = true;
    this.Name = MixedValues.GetMixedValue(this.items, e => e.Name);
    this.doNotPropagateRename = false;

    this.OnPropertyChanged(nameof(this.ItemsCount));
    this.OnPropertyChanged(nameof(this.HasItems));
    this.OnPropertyChanged(nameof(this.HasMultipleItems));

    // Log refreshed properties (include mixed name if available)
    this.LogRefreshedProperties(this.Name);
    }

    private void UpdatePropertyEditors()
    {
        var filteredEditors = this.FilterPropertyEditors();

        // Remove items that should no longer be there
        for (var i = this.PropertyEditors.Count - 1; i >= 0; i--)
        {
            var editor = this.PropertyEditors[i];
            if (!filteredEditors.Contains(editor))
            {
                this.PropertyEditors.RemoveAt(i);
            }
        }

        // Add new items
        foreach (var editor in filteredEditors.Where(editor => !this.PropertyEditors.Contains(editor)))
        {
            this.PropertyEditors.Add(editor);
        }
    }

    private void UpdatePropertyEditorsValues()
    {
        // Update values
        foreach (var editor in this.PropertyEditors)
        {
            editor.UpdateValues(this.items);
        }

        // Log that property editor values were updated
        this.LogPropertyEditorsValuesUpdated();
    }

    /// <summary>
    /// Called when the <see cref="Name"/> property changes.
    /// </summary>
    /// <param name="value">The new name value.</param>
    /// <remarks>
    /// This method propagates the name change to all items being edited, unless the change originated
    /// from one of the items itself (indicated by <see cref="doNotPropagateRename"/> being true).
    /// </remarks>
    partial void OnNameChanged(string? value)
    {
        if (value is null || this.doNotPropagateRename)
        {
            return;
        }

        // Log the propagation before applying
        this.LogNamePropagated(value);

        foreach (var entity in this.items)
        {
            entity.Name = value;
        }
    }
}
