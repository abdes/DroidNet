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

        this.Items = this.messenger.Send(new EntitySeletionRequestMessage()).SelectedEntities;
        this.messenger.Register<EntitySelectionChangedMessage>(this, (_, message) => this.Items = message.SelectedEntities);
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
    protected override Dictionary<Type, IPropertyEditor<GameEntity>> FilterPropertyEditors()
    {
        var filteredEditors = new Dictionary<Type, IPropertyEditor<GameEntity>>(AllPropertyEditors);
        var keysToCheck = new HashSet<Type>(AllPropertyEditors.Keys);

        foreach (var entity in this.Items)
        {
            foreach (var key in keysToCheck.ToList().Where(key => !entity.Components.Any(component => component.GetType() == key)))
            {
                _ = filteredEditors.Remove(key);
                _ = keysToCheck.Remove(key);
            }
        }

        return filteredEditors;
    }
}
