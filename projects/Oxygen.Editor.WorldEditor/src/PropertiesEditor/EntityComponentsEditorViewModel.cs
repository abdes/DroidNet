// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Mvvm.Converters;
using Oxygen.Editor.Projects;
using Oxygen.Editor.WorldEditor.Messages;

namespace Oxygen.Editor.WorldEditor.PropertiesEditor;

public sealed partial class EntityComponentsEditorViewModel : MultiSelectionDetails<GameEntity>, IDisposable
{
    private static readonly IDictionary<Type, IPropertyEditor<GameEntity>> AllPropertyEditors = new Dictionary<Type, IPropertyEditor<GameEntity>>()
    {
        { typeof(Transform), new TransformViewModel() },
    };

    private ICollection<GameEntity> items;
    private bool isDisposed;
    private readonly IMessenger messenger;

    /// <summary>
    /// Initializes a new instance of the <see cref="EntityComponentsEditorViewModel"/> class.
    /// </summary>
    /// <param name="vmToViewConverter"></param>
    /// <param name="messenger"></param>
    public EntityComponentsEditorViewModel(ViewModelToView vmToViewConverter, IMessenger messenger)
    {
        this.messenger = messenger;
        this.VmToViewConverter = vmToViewConverter;

        this.items = this.messenger.Send(new EntitySeletionRequestMessage()).SelectedEntities;
        this.UpdateItemsCollection(this.items);
        this.messenger.Register<EntitySelectionChangedMessage>(this, (_, message) =>
        {
            this.items = message.SelectedEntities;
            this.UpdateItemsCollection(this.items);
        });
    }

    /// <summary>
    /// Gets a viewmodel to view converter provided by the local Ioc container, which can resolve
    /// view from viewmodels registered locally. This converter must be used instead of the default
    /// Application converter.
    /// </summary>
    public ViewModelToView VmToViewConverter { get; }

    /// <inheritdoc/>
    public void Dispose()
    {
        if (this.isDisposed)
        {
            return;
        }

        this.messenger.UnregisterAll(this);

        this.isDisposed = true;
    }

    /// <inheritdoc/>
    protected override ICollection<IPropertyEditor<GameEntity>> FilterPropertyEditors()
    {
        var filteredEditors = new Dictionary<Type, IPropertyEditor<GameEntity>>(AllPropertyEditors);
        var keysToCheck = new HashSet<Type>(AllPropertyEditors.Keys);

        foreach (var entity in this.items)
        {
            // Filter out keys for which the entity does not have a component
            foreach (var key in keysToCheck.ToList().Where(key => entity.Components.All(component => component.GetType() != key)))
            {
                _ = filteredEditors.Remove(key);
                _ = keysToCheck.Remove(key);
            }
        }

        return filteredEditors.Values;
    }
}
