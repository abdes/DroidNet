// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Reactive.Concurrency;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.Assets;
using Oxygen.Editor.Input;
using Oxygen.Editor.Pickers;
using Oxygen.Editor.SceneInspector.Presentation;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Settings;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.SceneSettings;

/// <summary>
/// The scene's Play section: the camera a played scene starts viewing through and the input mapping contexts it
/// starts with. Every edit replaces the scene's play settings as one undoable command.
/// </summary>
public sealed partial class PlaySectionViewModel : ObservableObject, IDisposable
{
    private const string SearchText = "Play Starting camera Starting input contexts Priority Game start";

    private readonly Func<SceneAuthoring?>? authoringProvider;
    private readonly IAssetFeed? assetProvider;
    private readonly IScheduler observerScheduler;
    private readonly Func<AssetPath, InputContextSource> contextSource;
    private readonly IInputSources? inputSources;
    private readonly Func<NodeId, Task>? inspectSceneNode;
    private readonly Func<AssetPath, Task>? revealAsset;
    private readonly List<AssetEntry> assets = [];
    private IDisposable? assetSubscription;
    private IDisposable? inputSubscription;
    private bool catalogLoaded;
    private Scene? scene;
    private NodeId? automaticCamera;
    private Task pending = Task.CompletedTask;
    private bool isBusy;
    private bool inputEnabled = true;
    private bool updating;
    private bool disposed;
    private int candidateCount;

    /// <summary>Initializes a new instance of the <see cref="PlaySectionViewModel"/> class.</summary>
    /// <param name="authoringProvider">The authoring of the scene being inspected.</param>
    /// <param name="assetProvider">The shared content catalog that offers mapping contexts.</param>
    /// <param name="observerScheduler">The scheduler used for catalog notifications.</param>
    /// <param name="contextSource">Reads what the project says about a mapping context: whether an input file
    /// declares it, and its priority.</param>
    /// <param name="inspectSceneNode">Selects a node in the Scene Explorer.</param>
    /// <param name="revealAsset">Shows an asset in the Content Explorer.</param>
    /// <param name="inputSources">The project's input sources; a change to one reads the contexts again.</param>
    internal PlaySectionViewModel(
        Func<SceneAuthoring?>? authoringProvider,
        IAssetFeed? assetProvider,
        IScheduler observerScheduler,
        Func<AssetPath, InputContextSource>? contextSource,
        Func<NodeId, Task>? inspectSceneNode = null,
        Func<AssetPath, Task>? revealAsset = null,
        IInputSources? inputSources = null)
    {
        this.authoringProvider = authoringProvider;
        this.assetProvider = assetProvider;
        this.observerScheduler = observerScheduler;
        this.contextSource = contextSource ?? (static _ => InputContextSource.External);
        this.inspectSceneNode = inspectSceneNode;
        this.revealAsset = revealAsset;
        this.inputSources = inputSources;
    }

    /// <summary>Gets the starting camera list: Automatic, then every camera in hierarchy order.</summary>
    public ObservableCollection<StartingCameraChoice> CameraChoices { get; } = [];

    /// <summary>Gets or sets the chosen starting camera.</summary>
    [ObservableProperty]
    public partial StartingCameraChoice? SelectedCamera { get; set; }

    /// <summary>Gets the camera Automatic starts on, or an empty string when the scene has none.</summary>
    [ObservableProperty]
    public partial string AutomaticCameraName { get; private set; } = string.Empty;

    /// <summary>Gets a value indicating whether Automatic is chosen and resolves to a camera.</summary>
    [ObservableProperty]
    public partial bool ShowsAutomaticCamera { get; private set; }

    /// <summary>Gets a value indicating whether Automatic is chosen and the scene has no camera.</summary>
    [ObservableProperty]
    public partial bool ShowsNoCamera { get; private set; }

    /// <summary>Gets the problem with the starting camera, or an empty string.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasCameraError))]
    public partial string CameraError { get; private set; } = string.Empty;

    /// <summary>Gets the starting input contexts, highest priority first: the order the engine evaluates them.</summary>
    public ObservableCollection<StartingInputContextRowViewModel> Contexts { get; } = [];

    /// <summary>Gets the mapping contexts that can be added, grouped as Engine, Packages and Content.</summary>
    public AssetPickerModel ContextChoices { get; } = new(static asset => AssetChoice.For(asset, "Input mapping context", ""));

    /// <summary>Gets what Add does, or why it is unavailable.</summary>
    [ObservableProperty]
    public partial string AddToolTip { get; private set; } = string.Empty;

    /// <summary>Gets the latest edit failure, or an empty string.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasError))]
    public partial string ErrorMessage { get; private set; } = string.Empty;

    /// <summary>Gets a value indicating whether the starting camera has a problem.</summary>
    public bool HasCameraError => this.CameraError.Length > 0;

    /// <summary>Gets a value indicating whether an edit failed.</summary>
    public bool HasError => this.ErrorMessage.Length > 0;

    /// <summary>Gets a value indicating whether no input context starts.</summary>
    public bool HasNoContexts => this.Contexts.Count == 0;

    /// <summary>Gets a value indicating whether the play settings can be edited.</summary>
    public bool CanEdit => !this.disposed && !this.isBusy && this.inputEnabled
        && this.scene is { } current
        && this.authoringProvider?.Invoke() is { } authoring
        && ReferenceEquals(authoring.Scene, current);

    /// <summary>Gets a value indicating whether an input context can be added.</summary>
    public bool CanAddContext => this.CanEdit && this.candidateCount > 0;

    /// <summary>Gets completion of any play edit in flight.</summary>
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
        this.inputSubscription?.Dispose();
        this.inputSubscription = null;
        this.Bind(value: null);
    }

    /// <summary>Binds the scene whose play settings the section shows.</summary>
    /// <param name="value">The scene, or null when scene authoring is unavailable.</param>
    internal void Bind(Scene? value)
    {
        if (ReferenceEquals(this.scene, value))
        {
            return;
        }

        if (this.scene is { } previous)
        {
            previous.PropertyChanged -= this.OnSceneChanged;
        }

        this.scene = this.disposed ? null : value;
        if (this.scene is { } current)
        {
            current.PropertyChanged += this.OnSceneChanged;
            this.StartAssets();
            this.StartInputSources();
        }

        this.Refresh();
    }

    /// <summary>Enables or disables authoring with the parent inspector.</summary>
    /// <param name="enabled">Whether edits are allowed.</param>
    internal void SetInputEnabled(bool enabled)
    {
        this.inputEnabled = enabled;
        this.Refresh();
    }

    /// <summary>Shows the scene's current cameras and play settings.</summary>
    /// <remarks>Cameras are read again on every refresh: adding, renaming or reparenting one does not change the
    /// play settings, but changes what Automatic means and how cameras are named. The camera list is replaced only
    /// when that changes, so an open list and its selection survive the refreshes edits and the catalog cause.</remarks>
    internal void Refresh()
    {
        this.updating = true;
        try
        {
            this.RefreshCameras();
            this.RefreshContexts();
            this.RefreshCandidates();
        }
        finally
        {
            this.updating = false;
        }

        this.OnPropertyChanged(nameof(this.CanEdit));
        this.OnPropertyChanged(nameof(this.CanAddContext));
        this.AddContextCommand.NotifyCanExecuteChanged();
    }

    /// <summary>Gets whether the section matches the inspector search.</summary>
    /// <param name="query">The raw search query.</param>
    /// <returns><see langword="true"/> when every query token matches the section or one of its rows.</returns>
    internal bool MatchesSearch(string query)
    {
        var terms = query.Trim().Split([' ', '\t', '/', '_', '-'], StringSplitOptions.RemoveEmptyEntries)
            .Select(InspectorSearchModel.Normalize)
            .ToArray();
        if (terms.Length == 0)
        {
            return true;
        }

        return new[] { SearchText }
            .Concat(this.CameraChoices.Select(static choice => choice.Name))
            .Concat(this.Contexts.Select(static row => row.DisplayName))
            .Any(text =>
            {
                var normalized = InspectorSearchModel.Normalize(text);
                return terms.All(normalized.Contains);
            });
    }

    /// <summary>Removes a starting input context.</summary>
    /// <param name="context">The context.</param>
    /// <returns>The completion of the edit.</returns>
    internal Task RemoveContextAsync(AssetPath context)
        => this.EditContextsAsync(contexts => [.. contexts.Where(entry => entry.Context != context)]);

    /// <summary>Sets the priority a starting input context is added with.</summary>
    /// <param name="context">The context.</param>
    /// <param name="priority">The priority, or <see langword="null"/> for the context's own.</param>
    /// <returns>The completion of the edit.</returns>
    internal Task SetContextPriorityAsync(AssetPath context, int? priority)
        => this.EditContextsAsync(contexts =>
        {
            // Typing the context's own priority keeps following it.
            var own = this.contextSource(context).OwnPriority;
            var authored = priority == own ? null : priority;
            return [.. contexts.Select(entry => entry.Context == context ? entry with { Priority = authored } : entry)];
        });

    /// <summary>Shows a starting input context in the Content Explorer.</summary>
    /// <param name="context">The context.</param>
    /// <returns>The completion of the navigation.</returns>
    internal Task RevealContextAsync(AssetPath context) => this.revealAsset?.Invoke(context) ?? Task.CompletedTask;

    private static bool IsCamera(SceneNode node) => node.Components.OfType<CameraComponent>().Any();

    private static string AddToolTipFor(bool catalogLoaded, bool anyContext, bool anyCandidate) => (catalogLoaded, anyContext, anyCandidate) switch
    {
        (false, _, _) => "The project's input contexts are not read yet.",
        (_, false, _) => "The project has no input contexts. Write an input file to declare one.",
        (_, _, false) => "Every input context is already added.",
        _ => "Add an input context that is active when the game starts.",
    };

    partial void OnSelectedCameraChanged(StartingCameraChoice? value)
    {
        if (!this.updating && value is not null && this.scene is { } current && value.Id != current.StartingCamera)
        {
            _ = this.ApplyAsync(new SetStartingCamera(value.Id));
        }
    }

    [RelayCommand]
    private Task InspectAutomaticCameraAsync()
        => this.automaticCamera is { } camera && this.inspectSceneNode is { } inspect ? inspect(camera) : Task.CompletedTask;

    private bool CanAdd(AssetPath? context) => this.CanEdit && context is not null;

    [RelayCommand(CanExecute = nameof(CanAdd))]
    private Task AddContextAsync(AssetPath? context)
        => context is null
            ? Task.CompletedTask
            : this.EditContextsAsync(contexts => [.. contexts, new StartingInputContextData(context)]);

    private Task EditContextsAsync(Func<List<StartingInputContextData>, List<StartingInputContextData>> edit)
        => this.scene is { } current
            ? this.ApplyAsync(new SetStartingInputContexts(edit([.. current.StartingInputContexts])))
            : Task.CompletedTask;

    private Task ApplyAsync(ISceneEdit edit)
    {
        if (!this.CanEdit || this.authoringProvider?.Invoke() is not { } authoring)
        {
            this.Refresh();
            return Task.CompletedTask;
        }

        this.pending = this.ApplyCoreAsync(authoring, edit);
        return this.pending;
    }

    private async Task ApplyCoreAsync(SceneAuthoring authoring, ISceneEdit edit)
    {
        this.isBusy = true;
        this.ErrorMessage = string.Empty;
        try
        {
            var result = await authoring.ExecuteAsync(edit).ConfigureAwait(true);
            if (!result.Succeeded)
            {
                this.ErrorMessage = result.Diagnostics.FirstOrDefault(static diagnostic => diagnostic.IsError)?.Message ?? "The play settings could not be changed.";
            }
        }
        finally
        {
            this.isBusy = false;
            this.Refresh();
        }
    }

    private void RefreshCameras()
    {
        var cameras = this.scene?.AllNodes.Where(IsCamera).ToArray() ?? [];
        var authored = this.scene?.StartingCamera;
        var gone = authored is { } id && cameras.All(camera => camera.Id != id);
        var goneName = gone ? this.scene?.AllNodes.FirstOrDefault(node => node.Id == authored)?.Metadata.Name : null;

        // Automatic, every camera, and the authored camera when it is gone, so the list shows what is authored.
        var wanted = new List<(NodeId? Id, string Name)> { (null, "Automatic") };
        wanted.AddRange(cameras.Select(static camera => ((NodeId?)camera.Id, camera.Metadata.Name)));
        if (gone)
        {
            wanted.Add((authored, goneName ?? "Missing camera"));
        }

        if (!this.CameraChoices.Select(static choice => (choice.Id, choice.Name)).SequenceEqual(wanted))
        {
            this.CameraChoices.Clear();
            foreach (var (choiceId, name) in wanted)
            {
                this.CameraChoices.Add(new StartingCameraChoice(choiceId, name));
            }
        }

        this.SelectedCamera = this.CameraChoices.First(choice => choice.Id == authored);
        this.automaticCamera = authored is null ? cameras.FirstOrDefault()?.Id : null;
        this.AutomaticCameraName = authored is null ? cameras.FirstOrDefault()?.Metadata.Name ?? string.Empty : string.Empty;
        this.ShowsAutomaticCamera = authored is null && cameras.Length > 0;
        this.ShowsNoCamera = authored is null && cameras.Length == 0;
        this.CameraError = !gone
            ? string.Empty
            : goneName is null
                ? "The starting camera was deleted. Pick another camera, or return to Automatic."
                : $"'{goneName}' is no longer a camera. Pick another camera, or return to Automatic.";
    }

    private void RefreshContexts()
    {
        var entries = this.scene?.StartingInputContexts ?? [];
        var canEdit = this.CanEdit;
        var rows = entries
            .Select(entry =>
            {
                var row = this.Contexts.FirstOrDefault(existing => existing.Context == entry.Context) ?? new StartingInputContextRowViewModel(this, entry.Context);
                var asset = this.assets.FirstOrDefault(candidate => candidate.Identity == entry.Context);
                var source = this.contextSource(entry.Context);

                // A project context exists while an input file declares it, whatever the last cook left; another
                // exists while the catalog lists it.
                var missing = source.IsProjectContext ? source.IsUndeclared : this.catalogLoaded && asset is null;
                row.Update(new StartingInputContextRowViewModel.RowState(
                    asset?.DisplayName ?? Path.GetFileNameWithoutExtension(entry.Context.VirtualPath),
                    missing,
                    entry.Priority,
                    source.OwnPriority,
                    canEdit));
                return (Row: row, Priority: entry.Priority ?? source.OwnPriority);
            })

            // Highest priority first; equal priorities keep the order they were added in.
            .OrderByDescending(static item => item.Priority)
            .Select(static item => item.Row)
            .ToList();

        if (!rows.SequenceEqual(this.Contexts))
        {
            this.Contexts.Clear();
            foreach (var row in rows)
            {
                this.Contexts.Add(row);
            }

            this.OnPropertyChanged(nameof(this.HasNoContexts));
        }
    }

    private void RefreshCandidates()
    {
        var listed = this.scene?.StartingInputContexts.Select(static entry => entry.Context).ToHashSet() ?? [];
        var contexts = AssetRule.For(AssetKind.InputMappingContext).Apply(this.assets);
        var candidates = contexts
            .Where(asset => !listed.Contains(asset.Identity))
            .ToArray();
        this.ContextChoices.Update(candidates);
        this.candidateCount = candidates.Length;

        this.AddToolTip = AddToolTipFor(this.catalogLoaded, contexts.Count > 0, candidates.Length > 0);
    }

    private void StartAssets()
    {
        if (this.assetProvider is null || this.assetSubscription is not null || this.disposed)
        {
            return;
        }

        this.assetSubscription = this.assetProvider.Items
            .ObserveOn(this.observerScheduler)
            .Subscribe(this.OnAssets, this.OnCatalogFeedError);
    }

    private void StartInputSources()
    {
        if (this.inputSources is not { } sources || this.inputSubscription is not null || this.disposed)
        {
            return;
        }

        this.inputSubscription = Observable.FromEventPattern<InputSourceChangedEventArgs>(
                handler => sources.Changed += handler,
                handler => sources.Changed -= handler)
            .ObserveOn(this.observerScheduler)
            .Subscribe(_ => this.OnInputSourcesChanged());
    }

    // A context's declaration and its priority come from its input source.
    private void OnInputSourcesChanged()
    {
        if (!this.disposed)
        {
            this.Refresh();
        }
    }

    private void OnAssets(IReadOnlyList<AssetEntry> value)
    {
        if (this.disposed)
        {
            return;
        }

        this.assets.Clear();
        this.assets.AddRange(value);
        this.catalogLoaded = true;
        this.Refresh();
    }

    private void OnCatalogFeedError(Exception exception)
    {
        this.ErrorMessage = "The content catalog could not be read, so input contexts cannot be added. " + exception.Message;
        this.catalogLoaded = false;
        this.Refresh();
    }

    private void OnSceneChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (ReferenceEquals(sender, this.scene)
            && (string.IsNullOrEmpty(args.PropertyName)
                || string.Equals(args.PropertyName, nameof(Scene.StartingCamera), StringComparison.Ordinal)
                || string.Equals(args.PropertyName, nameof(Scene.StartingInputContexts), StringComparison.Ordinal)))
        {
            this.Refresh();
        }
    }
}
