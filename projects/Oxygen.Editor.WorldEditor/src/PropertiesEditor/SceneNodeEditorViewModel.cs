// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm.Converters;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Projects;
using Oxygen.Editor.WorldEditor.Messages;

namespace Oxygen.Editor.WorldEditor.PropertiesEditor;

/// <summary>
///     ViewModel for editing properties of selected SceneNode entities in the World Editor.
/// </summary>
public sealed partial class SceneNodeEditorViewModel : MultiSelectionDetails<SceneNode>, IDisposable
{
    private static readonly IDictionary<Type, IPropertyEditor<SceneNode>> AllPropertyEditors =
        new Dictionary<Type, IPropertyEditor<SceneNode>> { { typeof(Transform), new TransformViewModel() } };

    private readonly ILogger logger;

    private readonly IMessenger messenger;
    private bool isDisposed;

    private ICollection<SceneNode> items;

    /// <summary>
    /// Initializes a new instance of the <see cref="SceneNodeEditorViewModel"/> class.
    /// </summary>
    /// <param name="hosting">The hosting context for WinUI dispatching.</param>
    /// <param name="vmToViewConverter">The converter for resolving views from viewmodels.</param>
    /// <param name="messenger">The messenger for MVVM messaging.</param>
    /// <param name="loggerFactory">Optional logger factory for diagnostics.</param>
    public SceneNodeEditorViewModel(HostingContext hosting, ViewModelToView vmToViewConverter, IMessenger messenger, ILoggerFactory? loggerFactory = null)
        : base(loggerFactory)
    {
        this.logger = loggerFactory?.CreateLogger<SceneNodeEditorViewModel>() ?? NullLoggerFactory.Instance.CreateLogger<SceneNodeEditorViewModel>();

        this.messenger = messenger;
        this.VmToViewConverter = vmToViewConverter;

        this.items = this.messenger.Send(new SceneNodeSelectionRequestMessage()).SelectedEntities;
        this.UpdateItemsCollection(this.items);
        this.LogConstructed(this.items.Count);

        this.messenger.Register<SceneNodeSelectionChangedMessage>(this, (_, message) =>
            hosting.Dispatcher.TryEnqueue(() =>
            {
                this.items = message.SelectedEntities;
                this.LogSelectionChanged(this.items.Count);
                this.UpdateItemsCollection(this.items);
            }));
    }

    /// <summary>
    ///     Gets a viewmodel to view converter provided by the local Ioc container, which can resolve
    ///     view from viewmodels registered locally. This converter must be used instead of the default
    ///     Application converter.
    /// </summary>
    public ViewModelToView VmToViewConverter { get; }

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.isDisposed)
        {
            return;
        }

        this.messenger.UnregisterAll(this);
        this.LogDisposed();

        this.isDisposed = true;
    }

    /// <inheritdoc />
    protected override ICollection<IPropertyEditor<SceneNode>> FilterPropertyEditors()
    {
        var filteredEditors = new Dictionary<Type, IPropertyEditor<SceneNode>>(AllPropertyEditors);
        var keysToCheck = new HashSet<Type>(AllPropertyEditors.Keys);

        foreach (var entity in this.items)
        {
            // Filter out keys for which the entity does not have a component
            foreach (var key in keysToCheck.ToList()
                         .Where(key => entity.Components.All(component => component.GetType() != key)))
            {
                _ = filteredEditors.Remove(key);
                _ = keysToCheck.Remove(key);
            }
        }

        var before = AllPropertyEditors.Count;
        var after = filteredEditors.Count;
        this.LogFiltered(before, after);

        return filteredEditors.Values;
    }
}
