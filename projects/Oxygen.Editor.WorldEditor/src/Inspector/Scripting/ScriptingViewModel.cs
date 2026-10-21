// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Reactive.Concurrency;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.Mvvm.Messaging;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.Pickers;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Scripting;
using Oxygen.Editor.Scripting;
using Oxygen.Editor.Shell.Desktop;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Problems;
using Oxygen.Managed.Assets;
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

    private readonly Func<SceneAuthoring?>? authoringProvider;
    private readonly IAssetFeed? assetProvider;
    private readonly IScripts? scripts;
    private readonly ICodeEditorService? codeEditor;
    private readonly IMessenger? messenger;
    private readonly IProjectContextService? projects;

    // The library scripts read, or being read when the read is null.
    private readonly Dictionary<AssetPath, ScriptRead?> libraryScripts = [];
    private readonly List<AssetEntry> assets = [];
    private IReadOnlyList<AssetPath> scriptAssets = [];
    private IDisposable? assetSubscription;
    private IDisposable? scriptSubscription;
    private SceneNode? node;
    private ScriptingComponent? scripting;
    private Task pending = Task.CompletedTask;
    private bool isBusy;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="ScriptingViewModel"/> class.</summary>
    /// <param name="authoringProvider">The authoring of the scene being inspected.</param>
    /// <param name="assetProvider">The shared asset catalog, for script and asset pickers.</param>
    /// <param name="scripts">The scripts, for declared parameters and problems.</param>
    /// <param name="codeEditor">The code editor, for Edit.</param>
    /// <param name="messenger">The messenger, for New Script and Problems.</param>
    /// <param name="projects">The active project.</param>
    /// <param name="scheduler">The UI scheduler that catalog and script updates are observed on.</param>
    public ScriptingViewModel(
        Func<SceneAuthoring?>? authoringProvider = null,
        IAssetFeed? assetProvider = null,
        IScripts? scripts = null,
        ICodeEditorService? codeEditor = null,
        IMessenger? messenger = null,
        IProjectContextService? projects = null,
        IScheduler? scheduler = null)
    {
        this.authoringProvider = authoringProvider;
        this.assetProvider = assetProvider;
        this.scripts = scripts;
        this.codeEditor = codeEditor;
        this.messenger = messenger;
        this.projects = projects;
        var observer = scheduler ?? ImmediateScheduler.Instance;

        // The facts raise a change on the context that subscribed; the scheduler brings it to the UI thread wherever
        // the section is created.
        this.scriptSubscription = scripts is null ? null : Observable.FromEventPattern<ScriptChangedEventArgs>(
                handler => scripts.Changed += handler,
                handler => scripts.Changed -= handler)
            .ObserveOn(observer)
            .Subscribe(change => this.OnScriptsChanged(change.EventArgs));
        this.assetSubscription = assetProvider?.Items.ObserveOn(observer).Subscribe(this.OnAssets, static _ => { });
    }

    /// <inheritdoc />
    public override string Header => ScriptingComponent.DefaultName;

    /// <inheritdoc />
    public override string Description => "Scripts this node runs in the player, in slot order. Editor viewports do not run scripts.";

    /// <summary>Gets the slot cards, in slot order.</summary>
    public ObservableCollection<ScriptSlotCardViewModel> Slots { get; } = [];

    /// <summary>Gets the scripts a slot can run, grouped as Engine, Packages and Content.</summary>
    public AssetPickerModel ScriptChoices { get; } = new(static asset => AssetChoice.For(asset, "Script", "\uE943"));

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
        && this.authoringProvider?.Invoke() is { } authoring && ReferenceEquals(authoring.Scene, this.node?.Scene);

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
        this.scriptSubscription?.Dispose();
        this.scriptSubscription = null;
        this.assetSubscription?.Dispose();
        this.assetSubscription = null;
        this.Bind(value: null);
    }

    /// <summary>Applies one change to the slots as an undoable scene edit.</summary>
    /// <param name="change">Creates the edit of the selected node's slots.</param>
    /// <returns>The completion of the edit.</returns>
    internal async Task EditAsync(Func<NodeId, ISceneEdit> change)
    {
        if (!this.CanEdit || this.node is not { } owner || this.authoringProvider?.Invoke() is not { } authoring)
        {
            this.Rebuild();
            return;
        }

        this.SetBusy(busy: true);
        try
        {
            var edit = authoring.ExecuteAsync(change(owner.Id));
            this.pending = edit;
            var result = await edit.ConfigureAwait(true);
            this.ErrorMessage = result.Succeeded ? string.Empty : result.Diagnostics.FirstOrDefault(static diagnostic => diagnostic.IsError)?.Message ?? "The scripts could not be changed.";
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
            || project.FindByFilePath(path) is not { } location)
        {
            return;
        }

        var script = location.Identity;
        await this.EditAsync(node => new SetSlotScript(node, new ScriptSlotId(slotId), script)).ConfigureAwait(true);
    }

    /// <summary>Opens a script in the code editor.</summary>
    /// <param name="sourcePath">The script file.</param>
    /// <param name="line">The line, if any.</param>
    /// <returns>The completion of the request.</returns>
    internal async Task EditScriptAsync(FileSystemPath? sourcePath, int? line)
    {
        if (this.codeEditor is not null && sourcePath is not null)
        {
            _ = await this.codeEditor.OpenAsync(sourcePath.FullPath, line).ConfigureAwait(true);
        }
    }

    /// <summary>Shows a problem in the Problems panel.</summary>
    /// <param name="problem">The problem.</param>
    internal void ShowProblem(ScriptProblem problem)
        => _ = this.messenger?.Send(new ShowScriptProblemMessage(problem));

    /// <summary>Finds the assets an asset parameter of a native type accepts.</summary>
    /// <param name="assetType">The native asset type the parameter declares.</param>
    /// <returns>The assets, by name.</returns>
    internal IReadOnlyList<AssetEntry> FindAssets(byte assetType)
        => [.. AssetRule.ForCooked(AssetKinds.FromCookedType(assetType)).Apply(this.assets)
            .OrderBy(static asset => asset.DisplayName, StringComparer.OrdinalIgnoreCase)
            .ThenBy(static asset => asset.DisplayPath, StringComparer.OrdinalIgnoreCase)];

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
    internal bool ContainsNode(NodeId nodeId) => this.node?.Scene.AllNodes.Any(candidate => candidate.Id == nodeId) == true;

    /// <summary>Names a node of the scene for a node field.</summary>
    /// <param name="nodeId">The node.</param>
    /// <returns>The node's hierarchy path, or null when the scene no longer has it.</returns>
    internal string? DescribeNode(NodeId nodeId)
        => this.node?.Scene.AllNodes.FirstOrDefault(candidate => candidate.Id == nodeId) is { } found ? found.HierarchyPath() : null;

    /// <inheritdoc />
    protected override void OnInputEnabledChanged(bool enabled) => this.RefreshCanEdit();

    [RelayCommand]
    private Task AddSlotAsync() => this.EditAsync(static node => new AddScriptSlot(node, new ScriptSlotId(Guid.NewGuid())));

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

    private void OnScriptsChanged(ScriptChangedEventArgs args)
    {
        // No script named: the project changed, and with it the libraries it mounts.
        if (args.Script is null)
        {
            this.libraryScripts.Clear();
        }

        if (!this.disposed && this.scripting?.Slots.Any(slot => args.Script is null || slot.Script == args.Script) == true)
        {
            this.Rebuild();
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
        var scripts = this.FindAssets(ScriptAssetType);
        var identities = scripts.Select(static script => script.Identity).ToArray();
        if (!identities.SequenceEqual(this.scriptAssets))
        {
            // Mounted libraries changed, so library scripts are read again.
            this.libraryScripts.Clear();
            this.scriptAssets = identities;
        }

        this.ScriptChoices.Update(scripts);

        this.Rebuild();
    }

    // A script the project has no source of may come from a mounted library; it is queued for reading, and shown as
    // being read.
    private (ScriptRead? Script, bool IsReading) FindScript(AssetPath? script, List<AssetPath> toRead)
    {
        if (script is null || this.scripts is null)
        {
            return (null, false);
        }

        if (this.scripts.Find(script) is { } source)
        {
            return (ScriptRead.Found(source), false);
        }

        if (this.libraryScripts.TryGetValue(script, out var library))
        {
            return (library, library is null);
        }

        this.libraryScripts[script] = null;
        toRead.Add(script);
        return (null, true);
    }

    // A library script's cooked descriptor records its parameters; reading it rebuilds the cards that use it.
    private async Task ReadLibraryScriptAsync(IScripts library, AssetPath script)
    {
        ScriptRead read;
        try
        {
            read = await library.ReadLibraryAsync(script).ConfigureAwait(true);
        }
        catch (OperationCanceledException)
        {
            _ = this.libraryScripts.Remove(script);
            return;
        }

        if (this.disposed || !this.libraryScripts.TryGetValue(script, out var current) || current is not null)
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
        var toRead = new List<AssetPath>();
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

            var (script, isReading) = this.FindScript(slot.Script, toRead);
            card.Update(slot, index, slots.Length, script, isReading);
        }

        this.OnPropertyChanged(nameof(this.HasNoSlots));
        this.OnPropertyChanged(nameof(this.CanEdit));

        // Reads start once every card is shown, so a read that completes at once rebuilds after this pass.
        if (this.scripts is { } library)
        {
            foreach (var script in toRead.Distinct())
            {
                _ = this.ReadLibraryScriptAsync(library, script);
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
