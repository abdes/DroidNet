// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Globalization;
using System.Reactive.Concurrency;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Aura.Dialogs;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Pickers;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Instances;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.SceneInspector.Components.Instances;

/// <summary>
/// The Scene Instance section of an instance root (scene instances 8.5): its source, which the source picker swaps
/// for another model or authored scene (6.5), the number of changes the scene makes to the instance, <i>Unpack</i> and
/// <i>Unpack completely</i> (6.8), and, for an instanced scene, that its environment and start state apply only when it
/// runs as a level.
/// </summary>
/// <remarks>
/// A swap of an instance with changes first asks whether to keep them by name or discard them. Keeping them needs the
/// new source's structure, which the section reads first.
/// </remarks>
public sealed partial class SceneInstanceViewModel : ComponentPropertyEditor, IDisposable
{
    private readonly Func<SceneAuthoring?> authoringProvider;
    private readonly IProjectSession session;
    private readonly IAssetFeed? assets;
    private readonly IScheduler scheduler;
    private readonly IAuthoredReferences? references;
    private readonly ISceneStructures? structures;
    private readonly IInstancedNodeReader? reader;
    private readonly IDialogService? dialogs;
    private readonly IOperationResultPublisher? operationResults;
    private readonly IStatusReducer? statusReducer;
    private readonly ILogger logger;
    private IReadOnlyList<AssetEntry> latestAssets = [];
    private IDisposable? assetSubscription;
    private SceneInstanceComponent? instance;
    private NodeId? root;
    private bool isMixed;
    private bool isBusy;
    private bool disposed;
    private Task pending = Task.CompletedTask;

    /// <summary>Initializes a new instance of the <see cref="SceneInstanceViewModel"/> class.</summary>
    /// <param name="authoringProvider">The authoring of the scene being inspected.</param>
    /// <param name="session">The project session the scene belongs to.</param>
    /// <param name="scheduler">The scheduler catalog notifications are observed on.</param>
    /// <param name="services">The services the section reads sources and asks questions with.</param>
    /// <param name="loggerFactory">The logger factory.</param>
    internal SceneInstanceViewModel(
        Func<SceneAuthoring?> authoringProvider,
        IProjectSession session,
        IScheduler scheduler,
        InspectorInstanceServices services,
        ILoggerFactory? loggerFactory = null)
    {
        ArgumentNullException.ThrowIfNull(services);
        this.authoringProvider = authoringProvider ?? throw new ArgumentNullException(nameof(authoringProvider));
        this.session = session ?? throw new ArgumentNullException(nameof(session));
        this.scheduler = scheduler ?? throw new ArgumentNullException(nameof(scheduler));
        this.assets = services.Assets;
        this.references = services.References;
        this.structures = services.Structures;
        this.reader = services.Nodes;
        this.dialogs = services.Dialogs;
        this.operationResults = services.OperationResults;
        this.statusReducer = services.StatusReducer;
        this.logger = loggerFactory?.CreateLogger<SceneInstanceViewModel>() ?? NullLogger<SceneInstanceViewModel>.Instance;
    }

    /// <summary>Gets the sources the picker offers: the imported models, and the scenes that may be instanced here.</summary>
    public AssetPickerModel SourceChoices { get; } = InstanceSources.CreatePicker();

    /// <summary>Gets the source's file name, or what the selection shows when its instances differ.</summary>
    [ObservableProperty]
    public partial string SourceLabel { get; private set; } = string.Empty;

    /// <summary>Gets what kind of source it is, such as <c>Model</c>.</summary>
    [ObservableProperty]
    public partial string SourceKind { get; private set; } = string.Empty;

    /// <summary>Gets how many values the scene changes in the instance, such as <c>3 changes</c>.</summary>
    [ObservableProperty]
    public partial string ChangeCount { get; private set; } = string.Empty;

    /// <summary>Gets a value indicating whether the instanced source is an authored scene, whose level settings do not apply.</summary>
    [ObservableProperty]
    public partial bool IsPrefab { get; private set; }

    /// <summary>Gets why the last swap did not apply; empty when it did.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasError))]
    public partial string ErrorMessage { get; private set; } = string.Empty;

    /// <summary>Gets a value indicating whether the last swap did not apply.</summary>
    public bool HasError => this.ErrorMessage.Length > 0;

    /// <summary>Gets a value indicating whether the source can be swapped: one instance root of the inspected scene is selected.</summary>
    public bool CanSwap => !this.disposed && !this.isBusy && !this.isMixed && this.IsInputEnabled && this.instance is not null;

    /// <summary>
    /// Gets a value indicating whether the instance can be unpacked: one instance root of the inspected scene is selected,
    /// and the values of its nodes can be read.
    /// </summary>
    public bool CanUnpack => this.CanSwap && this.reader is not null;

    /// <inheritdoc />
    public override string Header => "Scene Instance";

    /// <inheritdoc />
    public override string Description => "The model or scene this node instances, how many of its values this scene changes, and unpacking it into the scene's own nodes.";

    /// <summary>Gets completion of the swap or unpack in flight.</summary>
    internal Task Pending => this.pending;

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.assetSubscription?.Dispose();
        this.assetSubscription = null;
        this.Observe(component: null);
    }

    /// <inheritdoc />
    public override void UpdateValues(ICollection<SceneNode> items)
    {
        ArgumentNullException.ThrowIfNull(items);
        var instances = items
            .Select(node => (Node: node, Instance: node.Components.OfType<SceneInstanceComponent>().FirstOrDefault()))
            .Where(static entry => entry.Instance is not null)
            .ToList();
        this.isMixed = instances.Select(static entry => entry.Instance!.Source).Distinct().Skip(1).Any();
        var single = instances.Count == 1 && this.ReferenceOf(instances[0].Node).IsOwned ? instances[0] : default;
        this.root = single.Node?.Id;
        this.Observe(single.Instance);
        this.ErrorMessage = string.Empty;
        this.StartAssets();
        this.Refresh(instances.Count == 0 ? null : instances[0].Instance);
    }

    /// <summary>Refreshes the sources the picker offers, as when it opens: the scenes that would instance this one are left out.</summary>
    /// <returns>A task that completes once the choices are current.</returns>
    internal async Task PrepareChoicesAsync()
    {
        if (this.references is { } saved)
        {
            await saved.RefreshAsync(CancellationToken.None).ConfigureAwait(true);
        }

        this.UpdateChoices();
    }

    /// <summary>Swaps the selected instance's source, asking first what to do with its changes.</summary>
    /// <param name="source">The new source: a model's source file or an authored scene.</param>
    /// <returns>A task that completes once the swap applied or was refused.</returns>
    internal Task SwapAsync(AssetPath source)
    {
        ArgumentNullException.ThrowIfNull(source);
        if (!this.CanSwap || this.instance is not { } current || this.root is not { } node || current.Source == source)
        {
            return Task.CompletedTask;
        }

        this.pending = this.SwapCoreAsync(node, current, source);
        return this.pending;
    }

    /// <summary>Unpacks the selected instance: its nodes become the scene's own, with their resolved values.</summary>
    /// <param name="depth">Whether the instances its source holds stay instances or are unpacked too.</param>
    /// <returns>A task that completes once the unpack applied or was refused.</returns>
    internal Task UnpackAsync(InstanceUnpack depth)
    {
        if (!this.CanUnpack || this.root is not { } node)
        {
            return Task.CompletedTask;
        }

        this.pending = this.UnpackCoreAsync(node, depth);
        return this.pending;
    }

    /// <inheritdoc />
    protected override void OnInputEnabledChanged(bool enabled) => this.NotifyCommandsChanged();

    private static int CountChanges(InstanceChanges changes)
        => changes.Nodes.Values.Sum(static node => node.Components.Values.Sum(static component => component.All.Count()) + node.Parameters.Values.Sum(static values => values.Count));

    private static string Changes(int count)
        => count switch
        {
            0 => "No changes",
            1 => "1 change",
            _ => string.Create(CultureInfo.InvariantCulture, $"{count} changes"),
        };

    private async Task SwapCoreAsync(NodeId node, SceneInstanceComponent current, AssetPath source)
    {
        this.isBusy = true;
        this.ErrorMessage = string.Empty;
        this.NotifyCommandsChanged();
        try
        {
            var swap = current.Changes.IsEmpty ? InstanceSwap.KeepByName : await this.AskAsync(current, source).ConfigureAwait(true);
            if (swap is not { } chosen || this.authoringProvider() is not { } authoring)
            {
                return;
            }

            if (chosen == InstanceSwap.KeepByName && !current.Changes.IsEmpty)
            {
                await this.ReadStructureAsync(source).ConfigureAwait(true);
            }

            this.LogSwapRequested(current.Source, source, chosen);
            var result = await authoring.ExecuteAsync(new SetInstanceSource(node, source, chosen)).ConfigureAwait(true);
            if (!result.Succeeded)
            {
                var error = result.Diagnostics.FirstOrDefault(static diagnostic => diagnostic.IsError);
                this.LogSwapRefused(error?.Code);
                this.ErrorMessage = error?.Message ?? $"The source could not be swapped for {source.Name}.";
            }
        }
        finally
        {
            this.isBusy = false;
            this.NotifyCommandsChanged();
        }
    }

    private async Task UnpackCoreAsync(NodeId node, InstanceUnpack depth)
    {
        if (this.authoringProvider() is not { } authoring)
        {
            return;
        }

        this.isBusy = true;
        this.ErrorMessage = string.Empty;
        this.NotifyCommandsChanged();
        try
        {
            this.LogUnpackRequested(depth);
            var result = await new InstanceConversion(this.session.Context, this.reader!).ExecuteUnpackAsync(authoring, node, depth).ConfigureAwait(true);
            if (!result.Succeeded)
            {
                var error = result.Diagnostics.FirstOrDefault(static diagnostic => diagnostic.IsError);
                this.LogUnpackRefused(error?.Code);
                this.ErrorMessage = error?.Message ?? "The instance could not be unpacked.";
            }

            if (this.operationResults is { } publisher && this.statusReducer is { } reducer)
            {
                SceneOperationResults.PublishEditResult(publisher, reducer, SceneOperationKinds.EditInstance, "Instance not unpacked", "Changes dropped", result, new AffectedScope { SceneId = authoring.Scene.Id });
            }
        }
        finally
        {
            this.isBusy = false;
            this.NotifyCommandsChanged();
        }
    }

    private void NotifyCommandsChanged()
    {
        this.OnPropertyChanged(nameof(this.CanSwap));
        this.OnPropertyChanged(nameof(this.CanUnpack));
    }

    // Asks what to do with the changes; null when the user cancels, or when no one can be asked.
    private async Task<InstanceSwap?> AskAsync(SceneInstanceComponent current, AssetPath source)
    {
        if (this.dialogs is not { } prompt)
        {
            return null;
        }

        var count = CountChanges(current.Changes);
        var spec = new DialogSpec(
            $"Swap to {source.Name}?",
            $"This instance has {Changes(count)} of {current.Source.Name}. Keep them on the nodes of {source.Name} that have the same names, or discard them. A change whose node is not found is kept, marked as carried from {current.Source.Name}.")
        {
            PrimaryButtonText = "Keep changes by name",
            SecondaryButtonText = "Discard changes",
            CloseButtonText = "Cancel",
            DefaultButton = DialogButton.Primary,
        };
        return await prompt.ShowAsync(spec).ConfigureAwait(true) switch
        {
            DialogButton.Primary => InstanceSwap.KeepByName,
            DialogButton.Secondary => InstanceSwap.Discard,
            _ => null,
        };
    }

    // The structure the swap finds the changes' nodes in: the one already read, else a read of it now.
    private async Task ReadStructureAsync(AssetPath source)
    {
        var project = this.session.Context;
        if (this.structures?.Find(project, source) is null && this.reader is { } nodes)
        {
            _ = await nodes.ReadStructureAsync(project, source).ConfigureAwait(true);
        }
    }

    private void Observe(SceneInstanceComponent? component)
    {
        if (ReferenceEquals(this.instance, component))
        {
            return;
        }

        if (this.instance is { } previous)
        {
            previous.PropertyChanged -= this.OnInstanceChanged;
        }

        this.instance = component;
        if (component is not null)
        {
            component.PropertyChanged += this.OnInstanceChanged;
        }
    }

    private void OnInstanceChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (ReferenceEquals(sender, this.instance)
            && (string.IsNullOrEmpty(args.PropertyName)
                || string.Equals(args.PropertyName, nameof(SceneInstanceComponent.Source), StringComparison.Ordinal)
                || string.Equals(args.PropertyName, nameof(SceneInstanceComponent.Changes), StringComparison.Ordinal)))
        {
            _ = this.scheduler.Schedule(() => this.Refresh(this.instance));
        }
    }

    private void Refresh(SceneInstanceComponent? shown)
    {
        if (this.disposed)
        {
            return;
        }

        var isModel = shown is { } model && AssetFileTypes.Is(model.Source.Name, AssetFileCategory.Model);
        this.SourceLabel = this.isMixed ? "Multiple sources" : shown?.Source.Name ?? string.Empty;
        this.SourceKind = this.isMixed || shown is null ? string.Empty : isModel ? "Model" : "Scene";
        this.ChangeCount = this.isMixed || shown is null ? string.Empty : Changes(CountChanges(shown.Changes));
        this.IsPrefab = !this.isMixed && shown is not null && !isModel;
        this.NotifyCommandsChanged();
        this.UpdateChoices();
    }

    private void StartAssets()
    {
        if (this.assets is null || this.assetSubscription is not null || this.disposed)
        {
            return;
        }

        this.assetSubscription = this.assets.Items
            .ObserveOn(this.scheduler)
            .Subscribe(this.OnAssets, this.LogCatalogFeedFailed);
    }

    private void OnAssets(IReadOnlyList<AssetEntry> value)
    {
        this.latestAssets = value;
        this.UpdateChoices();
    }

    private void UpdateChoices()
    {
        var scene = this.authoringProvider()?.Asset;
        this.SourceChoices.Update([
            .. InstanceSources.Models(this.latestAssets),
            .. InstanceSources.Scenes(this.latestAssets, source => scene is not null && InstanceRules.WouldCycle(scene, source, asset => this.references?.Of(asset) ?? [])),
        ]);
    }
}
