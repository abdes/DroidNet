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
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Inspector.Presentation;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Inspector.Environment;

/// <summary>
/// The scene's Play section: the camera a played scene starts viewing through and the input mapping contexts it
/// starts with. Every edit replaces the scene's play settings as one undoable command.
/// </summary>
public sealed partial class PlaySectionViewModel : ObservableObject, IDisposable
{
    private const string SearchText = "Play Starting camera Starting input contexts Priority Game start";

    private readonly ISceneDocumentCommandService? commandService;
    private readonly Func<SceneDocumentCommandContext?>? commandContextProvider;
    private readonly IAssetFeed? assetProvider;
    private readonly IScheduler observerScheduler;
    private readonly Func<Uri, InputContextSource> contextSource;
    private readonly Func<NodeId, Task>? inspectSceneNode;
    private readonly Func<Uri, Task>? revealAsset;
    private readonly List<AssetEntry> assets = [];
    private IDisposable? assetSubscription;
    private bool catalogLoaded;
    private Scene? scene;
    private NodeId? automaticCamera;
    private Task pending = Task.CompletedTask;
    private bool isBusy;
    private bool inputEnabled = true;
    private bool updating;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="PlaySectionViewModel"/> class.</summary>
    /// <param name="commandService">The command service that applies play edits.</param>
    /// <param name="commandContextProvider">The current document command context.</param>
    /// <param name="assetProvider">The shared content catalog that offers mapping contexts.</param>
    /// <param name="observerScheduler">The scheduler used for catalog notifications.</param>
    /// <param name="contextSource">Reads what the project says about a mapping context: whether an input file
    /// declares it, and its priority.</param>
    /// <param name="inspectSceneNode">Selects a node in the Scene Explorer.</param>
    /// <param name="revealAsset">Shows an asset in the Content Browser.</param>
    internal PlaySectionViewModel(
        ISceneDocumentCommandService? commandService,
        Func<SceneDocumentCommandContext?>? commandContextProvider,
        IAssetFeed? assetProvider,
        IScheduler observerScheduler,
        Func<Uri, InputContextSource>? contextSource,
        Func<NodeId, Task>? inspectSceneNode = null,
        Func<Uri, Task>? revealAsset = null)
    {
        this.commandService = commandService;
        this.commandContextProvider = commandContextProvider;
        this.assetProvider = assetProvider;
        this.observerScheduler = observerScheduler;
        this.contextSource = contextSource ?? (static _ => InputContextSource.External);
        this.inspectSceneNode = inspectSceneNode;
        this.revealAsset = revealAsset;
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

    /// <summary>Gets the mapping contexts that can be added.</summary>
    public ObservableCollection<AssetEntry> ContextCandidates { get; } = [];

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
        && this.commandService is not null
        && this.commandContextProvider?.Invoke() is { } context
        && ReferenceEquals(context.Scene, current);

    /// <summary>Gets a value indicating whether an input context can be added.</summary>
    public bool CanAddContext => this.CanEdit && this.ContextCandidates.Count > 0;

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
    internal Task RemoveContextAsync(Uri context)
        => this.EditContextsAsync(contexts => [.. contexts.Where(entry => entry.Context != context)]);

    /// <summary>Sets the priority a starting input context is added with.</summary>
    /// <param name="context">The context.</param>
    /// <param name="priority">The priority, or <see langword="null"/> for the context's own.</param>
    /// <returns>The completion of the edit.</returns>
    internal Task SetContextPriorityAsync(Uri context, int? priority)
        => this.EditContextsAsync(contexts =>
        {
            // Typing the context's own priority keeps following it.
            var own = this.contextSource(context).OwnPriority;
            var authored = priority == own ? null : priority;
            return [.. contexts.Select(entry => entry.Context == context ? entry with { Priority = authored } : entry)];
        });

    /// <summary>Shows a starting input context in the Content Browser.</summary>
    /// <param name="context">The context.</param>
    /// <returns>The completion of the navigation.</returns>
    internal Task RevealContextAsync(Uri context) => this.revealAsset?.Invoke(context) ?? Task.CompletedTask;

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
        if (!this.updating && value is not null && this.scene is { } current && value.Id != current.Play.StartingCamera)
        {
            _ = this.ApplyAsync(current.Play with { StartingCamera = value.Id });
        }
    }

    [RelayCommand]
    private Task InspectAutomaticCameraAsync()
        => this.automaticCamera is { } camera && this.inspectSceneNode is { } inspect ? inspect(camera) : Task.CompletedTask;

    private bool CanAdd(AssetEntry? candidate) => this.CanEdit && candidate is not null;

    [RelayCommand(CanExecute = nameof(CanAdd))]
    private Task AddContextAsync(AssetEntry? candidate)
        => candidate is null
            ? Task.CompletedTask
            : this.EditContextsAsync(contexts => [.. contexts, new StartingInputContextData(candidate.IdentityUri)]);

    private Task EditContextsAsync(Func<List<StartingInputContextData>, List<StartingInputContextData>> edit)
        => this.scene is { } current
            ? this.ApplyAsync(current.Play with { StartingInputContexts = edit([.. current.Play.StartingInputContexts]) })
            : Task.CompletedTask;

    private Task ApplyAsync(ScenePlayData play)
    {
        if (!this.CanEdit || this.commandService is not { } service || this.commandContextProvider?.Invoke() is not { } context)
        {
            this.Refresh();
            return Task.CompletedTask;
        }

        this.pending = this.ApplyCoreAsync(service, context, play);
        return this.pending;
    }

    private async Task ApplyCoreAsync(ISceneDocumentCommandService service, SceneDocumentCommandContext context, ScenePlayData play)
    {
        this.isBusy = true;
        this.ErrorMessage = string.Empty;
        try
        {
            var result = await service.EditScenePlayAsync(context, play).ConfigureAwait(true);
            if (!result.Succeeded)
            {
                this.ErrorMessage = result.ValidationMessage ?? "The play settings could not be changed.";
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
        var authored = this.scene?.Play.StartingCamera;
        var gone = authored is { } id && cameras.All(camera => camera.Id != id);
        var goneName = gone ? this.scene?.AllNodes.FirstOrDefault(node => node.Id == authored)?.Name : null;

        // Automatic, every camera, and the authored camera when it is gone, so the list shows what is authored.
        var wanted = new List<(NodeId? Id, string Name)> { (null, "Automatic") };
        wanted.AddRange(cameras.Select(static camera => ((NodeId?)camera.Id, camera.Name)));
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
        this.AutomaticCameraName = authored is null ? cameras.FirstOrDefault()?.Name ?? string.Empty : string.Empty;
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
        var entries = this.scene?.Play.StartingInputContexts ?? [];
        var canEdit = this.CanEdit;
        var rows = entries
            .Select(entry =>
            {
                var row = this.Contexts.FirstOrDefault(existing => existing.Context == entry.Context) ?? new StartingInputContextRowViewModel(this, entry.Context);
                var asset = this.assets.FirstOrDefault(candidate => candidate.IdentityUri == entry.Context || candidate.CookedUri == entry.Context);
                var source = this.contextSource(entry.Context);

                // A project context exists while an input file declares it, whatever the last cook left; another
                // exists while the catalog lists it.
                var missing = source.IsProjectContext ? source.IsUndeclared : this.catalogLoaded && asset is null;
                row.Update(new StartingInputContextRowViewModel.RowState(
                    asset?.DisplayName ?? Path.GetFileNameWithoutExtension(Uri.UnescapeDataString(entry.Context.AbsolutePath)),
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
        var listed = this.scene?.Play.StartingInputContexts.Select(static entry => entry.Context).ToHashSet() ?? [];
        var contexts = AssetRule.For(AssetKind.InputMappingContext).Apply(this.assets);
        var candidates = contexts
            .Where(asset => !listed.Contains(asset.IdentityUri))
            .ToArray();
        if (!candidates.Select(static asset => asset.IdentityUri).SequenceEqual(this.ContextCandidates.Select(static asset => asset.IdentityUri)))
        {
            this.ContextCandidates.Clear();
            foreach (var candidate in candidates)
            {
                this.ContextCandidates.Add(candidate);
            }
        }

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
            && (string.IsNullOrEmpty(args.PropertyName) || string.Equals(args.PropertyName, nameof(Scene.Play), StringComparison.Ordinal)))
        {
            this.Refresh();
        }
    }
}
