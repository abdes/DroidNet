// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Reactive.Concurrency;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.Mvvm.Messaging;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentBrowser.Scripts;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Problems;
using Oxygen.Editor.World.Scripting;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Inspector.Scripting;

/// <summary>
/// The Scripting section of a node: one card per script slot, in order. Every change is one undoable scene edit.
/// Scripts run in the player only, so nothing here reaches the editor viewports.
/// </summary>
public sealed partial class ScriptingViewModel : ComponentPropertyEditor, IDisposable
{
    /// <summary>The native asset type of scripts.</summary>
    private const byte ScriptAssetType = 4;

    private readonly ISceneDocumentCommandService? commands;
    private readonly Func<SceneDocumentCommandContext?>? contextProvider;
    private readonly IContentBrowserAssetProvider? assetProvider;
    private readonly IScriptAnalysisService? analysis;
    private readonly ICodeEditorService? codeEditor;
    private readonly IMessenger? messenger;
    private readonly IProjectContextService? projects;
    private readonly IScriptDeclarationProvider? declarationProvider;
    private readonly Dictionary<Uri, ScriptDeclarations> libraryScripts = [];
    private readonly List<ContentBrowserAssetItem> assets = [];
    private readonly IDisposable? analysisSubscription;
    private IDisposable? assetSubscription;
    private SceneNode? node;
    private ScriptingComponent? scripting;
    private Task pending = Task.CompletedTask;
    private bool isBusy;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="ScriptingViewModel"/> class.</summary>
    /// <param name="commands">The scene command service.</param>
    /// <param name="contextProvider">The current document command context.</param>
    /// <param name="assetProvider">The shared asset catalog, for script and asset pickers.</param>
    /// <param name="analysis">The script analysis, for declared parameters and problems.</param>
    /// <param name="codeEditor">The code editor, for Edit.</param>
    /// <param name="messenger">The messenger, for New Script and Problems.</param>
    /// <param name="projects">The active project.</param>
    /// <param name="scheduler">The UI scheduler that catalog and analysis updates are observed on.</param>
    /// <param name="declarationProvider">The declarations of library scripts, which have no source to analyze.</param>
    public ScriptingViewModel(
        ISceneDocumentCommandService? commands = null,
        Func<SceneDocumentCommandContext?>? contextProvider = null,
        IContentBrowserAssetProvider? assetProvider = null,
        IScriptAnalysisService? analysis = null,
        ICodeEditorService? codeEditor = null,
        IMessenger? messenger = null,
        IProjectContextService? projects = null,
        IScheduler? scheduler = null,
        IScriptDeclarationProvider? declarationProvider = null)
    {
        this.commands = commands;
        this.contextProvider = contextProvider;
        this.assetProvider = assetProvider;
        this.analysis = analysis;
        this.codeEditor = codeEditor;
        this.messenger = messenger;
        this.projects = projects;
        this.declarationProvider = declarationProvider;
        var observer = scheduler ?? ImmediateScheduler.Instance;
        this.analysisSubscription = analysis?.Changes.ObserveOn(observer).Subscribe(this.OnAnalyzed, static _ => { });
        this.assetSubscription = assetProvider?.Items.ObserveOn(observer).Subscribe(this.OnAssets, static _ => { });
    }

    /// <inheritdoc />
    public override string Header => ScriptingComponent.DefaultName;

    /// <inheritdoc />
    public override string Description => "Scripts this node runs in the player, in slot order. Editor viewports do not run scripts.";

    /// <summary>Gets the slot cards, in slot order.</summary>
    public ObservableCollection<ScriptSlotCardViewModel> Slots { get; } = [];

    /// <summary>Gets the scripts a slot can run.</summary>
    public ObservableCollection<ScriptAssetChoice> ScriptChoices { get; } = [];

    /// <summary>Gets the message shown instead of the slots, such as for a multi-node selection.</summary>
    [ObservableProperty]
    public partial string SelectionMessage { get; private set; } = string.Empty;

    /// <summary>Gets the reason the last change was refused, or empty.</summary>
    [ObservableProperty]
    public partial string ErrorMessage { get; private set; } = string.Empty;

    /// <summary>Gets a value indicating whether one node is selected, so its slots are shown.</summary>
    public bool ShowsSlots => this.SelectionMessage.Length == 0;

    /// <summary>Gets a value indicating whether the section shows a message instead of the slots.</summary>
    public bool HasSelectionMessage => !this.ShowsSlots;

    /// <summary>Gets a value indicating whether a change was refused.</summary>
    public bool HasError => this.ErrorMessage.Length > 0;

    /// <summary>Gets a value indicating whether the node has no slots.</summary>
    public bool HasNoSlots => this.ShowsSlots && this.Slots.Count == 0;

    /// <summary>Gets a value indicating whether slots can be changed.</summary>
    public bool CanEdit => !this.disposed && !this.isBusy && this.IsInputEnabled && this.scripting is not null
        && this.commands is not null && this.contextProvider?.Invoke() is { } context && ReferenceEquals(context.Scene, this.node?.Scene);

    /// <summary>Gets completion of the change in flight.</summary>
    internal Task Pending => this.pending;

    /// <inheritdoc />
    public override void UpdateValues(ICollection<SceneNode> items)
    {
        ArgumentNullException.ThrowIfNull(items);
        var single = items.Count == 1 ? items.First() : null;
        this.SelectionMessage = single is null ? "Select one node to edit its scripts." : string.Empty;
        this.Bind(single);
        this.Rebuild();
    }

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.analysisSubscription?.Dispose();
        this.assetSubscription?.Dispose();
        this.assetSubscription = null;
        this.Bind(null);
    }

    /// <summary>Applies one change to the slots as an undoable scene edit.</summary>
    /// <param name="change">Computes the new slots from the current ones.</param>
    /// <returns>The completion of the edit.</returns>
    internal async Task EditAsync(Func<ImmutableArray<ScriptSlot>, ImmutableArray<ScriptSlot>> change)
    {
        if (!this.CanEdit || this.scripting is not { } component || this.node is not { } owner
            || this.commands is not { } service || this.contextProvider?.Invoke() is not { } context)
        {
            this.Rebuild();
            return;
        }

        var slots = change(component.Slots);
        if (slots.SequenceEqual(component.Slots))
        {
            this.Rebuild();
            return;
        }

        this.SetBusy(busy: true);
        try
        {
            var edit = service.EditScriptSlotsAsync(context, owner.Id, slots);
            this.pending = edit;
            var result = await edit.ConfigureAwait(true);
            this.ErrorMessage = result.Succeeded ? string.Empty : result.ValidationMessage ?? "The scripts could not be changed.";
        }
        finally
        {
            this.pending = Task.CompletedTask;
            this.SetBusy(busy: false);
            this.Rebuild();
        }
    }

    /// <summary>Creates a script with New Script and runs it in a slot.</summary>
    /// <param name="slotId">The slot.</param>
    /// <returns>The completion of the creation and the edit.</returns>
    internal async Task CreateScriptForAsync(Guid slotId)
    {
        if (this.messenger is null || this.projects?.ActiveProject is not { } project)
        {
            return;
        }

        var request = this.messenger.Send(new CreateScriptRequestMessage());
        if (!request.HasReceivedResponse)
        {
            this.ErrorMessage = "New Script is available once the Content Browser is open.";
            return;
        }

        if (await request.Response.ConfigureAwait(true) is not { } path
            || RelocationPaths.ToVirtual(project, path) is not { } virtualPath)
        {
            return;
        }

        var script = new Uri($"{AssetUris.Scheme}://{ScriptSources.ToCookedScriptPath(virtualPath)}");
        await this.EditAsync(slots => ScriptSlotEdits.SetScript(slots, slotId, script)).ConfigureAwait(true);
    }

    /// <summary>Opens a script in the code editor.</summary>
    /// <param name="sourcePath">The script file.</param>
    /// <param name="line">The line, if any.</param>
    /// <returns>The completion of the request.</returns>
    internal async Task EditScriptAsync(string? sourcePath, int? line)
    {
        if (this.codeEditor is not null && sourcePath is not null)
        {
            _ = await this.codeEditor.OpenAsync(sourcePath, line).ConfigureAwait(true);
        }
    }

    /// <summary>Shows a problem in the Problems panel.</summary>
    /// <param name="problem">The problem.</param>
    internal void ShowProblem(ScriptProblem problem)
        => _ = this.messenger?.Send(new ShowScriptProblemMessage(problem));

    /// <summary>Finds the assets an asset parameter of a native type accepts.</summary>
    /// <param name="assetType">The native asset type the parameter declares.</param>
    /// <returns>The assets, by name.</returns>
    internal IEnumerable<ScriptAssetChoice> FindAssets(byte assetType)
    {
        var kind = AssetIdentityReducer.GetCookedKind(assetType);
        return this.assets.Where(asset => asset.Kind == kind && asset.IsSelectable && (kind == AssetKind.Script || asset.CookedUri is not null))
            .Select(static asset => new ScriptAssetChoice(asset.Kind == AssetKind.Script ? asset.IdentityUri : asset.CookedUri ?? asset.IdentityUri, asset.DisplayName, asset.DisplayPath, asset.Kind))
            .DistinctBy(static choice => choice.Uri)
            .OrderBy(static choice => choice.Name, StringComparer.OrdinalIgnoreCase)
            .ThenBy(static choice => choice.Path, StringComparer.OrdinalIgnoreCase);
    }

    /// <summary>Finds the scene's nodes whose name or path contains a search text.</summary>
    /// <param name="query">The search text; empty finds every node.</param>
    /// <returns>The nodes, in hierarchy order.</returns>
    internal IEnumerable<ScriptNodeChoice> FindNodes(string query)
    {
        if (this.node?.Scene is not { } scene)
        {
            return [];
        }

        var terms = query.Split(' ', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        return scene.AllNodes.Select(static candidate => new ScriptNodeChoice(candidate.Id, candidate.Name, candidate.HierarchyPath()))
            .Where(choice => terms.All(term => choice.Path.Contains(term, StringComparison.OrdinalIgnoreCase)));
    }

    /// <summary>Checks whether a node belongs to the selected node's scene.</summary>
    /// <param name="nodeId">The node.</param>
    /// <returns>Whether the scene contains it.</returns>
    internal bool ContainsNode(Guid nodeId) => this.node?.Scene.AllNodes.Any(candidate => candidate.Id == nodeId) == true;

    /// <summary>Names a node of the scene for a node field.</summary>
    /// <param name="nodeId">The node.</param>
    /// <returns>The node's hierarchy path, or null when the scene no longer has it.</returns>
    internal string? DescribeNode(Guid nodeId)
        => this.node?.Scene.AllNodes.FirstOrDefault(candidate => candidate.Id == nodeId) is { } found ? found.HierarchyPath() : null;

    /// <inheritdoc />
    protected override void OnInputEnabledChanged(bool enabled) => this.RefreshCanEdit();

    [RelayCommand]
    private Task AddSlotAsync() => this.EditAsync(ScriptSlotEdits.Add);

    partial void OnSelectionMessageChanged(string value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.ShowsSlots));
        this.OnPropertyChanged(nameof(this.HasSelectionMessage));
        this.OnPropertyChanged(nameof(this.HasNoSlots));
    }

    partial void OnErrorMessageChanged(string value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.HasError));
    }

    private void Bind(SceneNode? value)
    {
        var component = value?.Components.OfType<ScriptingComponent>().FirstOrDefault();
        if (ReferenceEquals(component, this.scripting) && ReferenceEquals(value, this.node))
        {
            return;
        }

        if (this.scripting is { } previous)
        {
            previous.PropertyChanged -= this.OnScriptingChanged;
        }

        this.node = value;
        this.scripting = component;
        this.ErrorMessage = string.Empty;
        component?.PropertyChanged += this.OnScriptingChanged;
    }

    private void OnScriptingChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (string.IsNullOrEmpty(args.PropertyName) || string.Equals(args.PropertyName, nameof(ScriptingComponent.Slots), StringComparison.Ordinal))
        {
            this.Rebuild();
        }
    }

    private void OnAnalyzed(ScriptAnalysisSnapshot snapshot)
    {
        if (!this.disposed && this.scripting?.Slots.Any(slot => slot.Script == snapshot.ScriptUri) == true)
        {
            this.Rebuild();
        }
    }

    private void OnAssets(IReadOnlyList<ContentBrowserAssetItem> value)
    {
        if (this.disposed)
        {
            return;
        }

        this.assets.Clear();
        this.assets.AddRange(value);
        var scripts = this.FindAssets(ScriptAssetType).ToArray();
        if (!scripts.SequenceEqual(this.ScriptChoices))
        {
            // Mounted libraries changed, so library scripts are read again.
            this.libraryScripts.Clear();
            this.ScriptChoices.Clear();
            foreach (var script in scripts)
            {
                this.ScriptChoices.Add(script);
            }
        }

        this.Rebuild();
    }

    // A script without analysis may come from a mounted library; it is queued for reading, and shown as loading.
    private ScriptDeclarations FindDeclarations(Uri? script, List<Uri> toRead)
    {
        if (script is null)
        {
            return ScriptDeclarations.Unknown;
        }

        if (this.analysis?.Current.FirstOrDefault(snapshot => !snapshot.IsRemoved && snapshot.ScriptUri == script) is { } source)
        {
            return ScriptDeclarations.FromSource(source);
        }

        if (this.libraryScripts.TryGetValue(script, out var library))
        {
            return library;
        }

        if (this.declarationProvider is null || this.projects?.ActiveProject is null)
        {
            return ScriptDeclarations.Unknown;
        }

        this.libraryScripts[script] = ScriptDeclarations.LibraryLoading;
        toRead.Add(script);
        return ScriptDeclarations.LibraryLoading;
    }

    // A library script's cooked descriptor records its parameters; reading it rebuilds the cards that use it.
    private async Task ReadLibraryScriptAsync(IScriptDeclarationProvider provider, ProjectContext project, Uri script)
    {
        ScriptDeclarations read;
        try
        {
            read = await provider.ReadAsync(project, script).ConfigureAwait(true) is { } cooked
                ? ScriptDeclarations.FromLibrary(cooked)
                : ScriptDeclarations.Unknown;
        }
        catch (OperationCanceledException)
        {
            _ = this.libraryScripts.Remove(script);
            return;
        }
        catch (Exception exception) when (exception is InvalidDataException or IOException or InvalidOperationException or ContentPipelineTerminationException)
        {
            read = ScriptDeclarations.LibraryUnavailable(exception.Message);
        }

        if (this.disposed || !this.libraryScripts.TryGetValue(script, out var current) || !ReferenceEquals(current, ScriptDeclarations.LibraryLoading))
        {
            return;
        }

        this.libraryScripts[script] = read;
        this.Rebuild();
    }

    private void Rebuild()
    {
        if (this.disposed)
        {
            return;
        }

        var slots = this.ShowsSlots && this.scripting is { } component ? component.Slots : [];
        var toRead = new List<Uri>();
        for (var index = this.Slots.Count - 1; index >= 0; index--)
        {
            if (!slots.Any(slot => slot.Id == this.Slots[index].SlotId))
            {
                this.Slots.RemoveAt(index);
            }
        }

        for (var index = 0; index < slots.Length; index++)
        {
            var slot = slots[index];
            var card = this.Slots.FirstOrDefault(candidate => candidate.SlotId == slot.Id);
            if (card is null)
            {
                card = new ScriptSlotCardViewModel(this, slot);
                this.Slots.Insert(Math.Min(index, this.Slots.Count), card);
            }
            else if (this.Slots.IndexOf(card) != index)
            {
                this.Slots.Move(this.Slots.IndexOf(card), index);
            }

            card.Update(slot, index, slots.Length, this.FindDeclarations(slot.Script, toRead));
        }

        this.OnPropertyChanged(nameof(this.HasNoSlots));
        this.OnPropertyChanged(nameof(this.CanEdit));

        // Reads start once every card is shown, so a read that completes at once rebuilds after this pass.
        if (this.declarationProvider is { } provider && this.projects?.ActiveProject is { } project)
        {
            foreach (var script in toRead.Distinct())
            {
                _ = this.ReadLibraryScriptAsync(provider, project, script);
            }
        }
    }

    private void SetBusy(bool busy)
    {
        this.isBusy = busy;
        this.RefreshCanEdit();
    }

    private void RefreshCanEdit()
    {
        this.OnPropertyChanged(nameof(this.CanEdit));
        foreach (var card in this.Slots)
        {
            card.RefreshCanEdit();
        }
    }
}
