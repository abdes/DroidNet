// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.ComponentModel;
using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.Core;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.WorldEditor.PropertiesEditor;

public abstract class MultiSelectionDetails<T> : ObservableObject
    where T : GameObject
{
    private IList<T> items = [];
    private string? name;

    public ObservableCollection<IPropertyEditor<T>> PropertyEditors { get; } = [];

    public string? Name
    {
        get => this.name;
        protected set => this.SetProperty(ref this.name, value);
    }

    public bool HasItems => this.Items.Count > 0;

    public Type? ItemsType { get; }

    public IList<T> Items
    {
        get => this.items;
        protected set => this.SetProperty(ref this.items, value);
    }

    public bool HasMultipleItems => this.Items.Count > 1;

    public int ItemsCount => this.Items.Count;

    internal static string? GetMixedValue(IList<T> entities, Func<T, string> getProperty)
    {
        if (entities.Count == 0)
        {
            return null;
        }

        var value = getProperty(entities[0]);
        return entities.Skip(1).Any(entity => !string.Equals(getProperty(entity), value, StringComparison.Ordinal)) ? null : value;
    }

    internal static bool? GetMixedValue(IList<T> entities, Func<T, bool> getProperty)
    {
        if (entities.Count == 0)
        {
            return null;
        }

        var value = getProperty(entities[0]);
        return entities.Skip(1).Any(entity => getProperty(entity) != value) ? null : value;
    }

    internal static float? GetMixedValue(IList<T> entities, Func<T, float> getProperty)
    {
        if (entities.Count == 0)
        {
            return null;
        }

        var value = getProperty(entities[0]);
        return entities.Skip(1).Any(entity => !getProperty(entity).IsSameAs(value)) ? null : value;
    }

    /// <inheritdoc/>
    protected override void OnPropertyChanged(PropertyChangedEventArgs e)
    {
        base.OnPropertyChanged(e);

        if (string.Equals(e.PropertyName, nameof(this.Items), StringComparison.Ordinal))
        {
            this.OnPropertyChanged(new PropertyChangedEventArgs(nameof(this.ItemsCount)));
            this.OnPropertyChanged(new PropertyChangedEventArgs(nameof(this.HasItems)));
            this.OnPropertyChanged(new PropertyChangedEventArgs(nameof(this.HasMultipleItems)));

            this.UpdateProperties();
            this.UpdatePropertyEditors();
            this.UpdatePropertyEditorsValues();
        }
    }

    private void UpdatePropertyEditors()
    {
        var filteredEditors = this.FilterPropertyEditors();

        // Remove items that should no longer be there
        for (var i = this.PropertyEditors.Count - 1; i >= 0; i--)
        {
            var editor = this.PropertyEditors[i];
            if (!filteredEditors.ContainsValue(editor))
            {
                this.PropertyEditors.RemoveAt(i);
            }
        }

        // Add new items
        foreach (var editor in filteredEditors.Values.Where(editor => !this.PropertyEditors.Contains(editor)))
        {
            this.PropertyEditors.Add(editor);
        }
    }

    protected abstract Dictionary<Type, IPropertyEditor<T>> FilterPropertyEditors();

    private void UpdatePropertyEditorsValues()
    {
        // Update values
        foreach (var editor in this.PropertyEditors)
        {
            editor.UpdateValues(this.Items);
        }
    }

    protected virtual void UpdateProperties()
    {
        this.Name = GetMixedValue(this.Items, e => e.Name);

        this.OnPropertyChanged(nameof(this.ItemsCount));
        this.OnPropertyChanged(nameof(this.HasItems));
        this.OnPropertyChanged(nameof(this.HasMultipleItems));
    }
}
