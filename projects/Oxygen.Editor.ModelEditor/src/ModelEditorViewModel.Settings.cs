// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ModelEditor.Importing;
using Oxygen.Editor.ModelEditor.Settings;
using Oxygen.Editor.Models;
using Oxygen.Editor.Models.Documents;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ModelEditor;

/// <summary>
/// The import settings panel beside the viewport (model editor 5.3): the model's import settings, its node continuity
/// and the selected mesh's and texture's groups, each edit one step of the document's history; the banner that says the
/// view shows the last import until Save (ME-6); and the toolbar's count of the model's kept problems (M3).
/// </summary>
public sealed partial class ModelEditorViewModel
{
    private const string ImportSettingsSelectionSource = "ImportSettings";

    private ModelImportFile? settingsFile;
    private ImmutableDictionary<Guid, ImmutableArray<string>>? materialTextures;
    private bool applyingEdit;
    private int meshRead;

    /// <summary>Gets the import settings panel.</summary>
    public ModelImportSettingsPanelViewModel Settings { get; }

    /// <summary>Gets the panel's width and collapse, per user and shared by every model document.</summary>
    public ModelEditorLayout Layout => this.services.Layout;

    /// <summary>Gets a value indicating whether the model has an import record whose settings the panel edits.</summary>
    [ObservableProperty]
    public partial bool HasImportSettings { get; private set; }

    /// <summary>Gets a value indicating whether the banner shows: the settings changed, and the view shows the last import.</summary>
    public bool IsShowingLastImport => this.Document.IsDirty && this.HasImportSettings;

    /// <summary>Gets the number of the model's kept import problems.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasProblems), nameof(ProblemCountText))]
    public partial int ProblemCount { get; private set; }

    /// <summary>Gets a value indicating whether the model has kept import problems, which the toolbar's status counts.</summary>
    public bool HasProblems => this.ProblemCount != 0;

    /// <summary>Gets the toolbar's problem count, such as "3 problems".</summary>
    public string ProblemCountText => string.Create(CultureInfo.CurrentCulture, $"{this.ProblemCount} {(this.ProblemCount == 1 ? "problem" : "problems")}");

    private ModelContent Model => (ModelContent)this.Document.Content;

    private static string NameOf(ModelImportSettings record)
        => record.Settings.TryGetProperty(ModelImportSettingCatalog.NameKey, out var name) && name.GetString() is { Length: > 0 } text
            ? text
            : string.Empty;

    // The folder a model's source sits in, as the import names a model's destination.
    private static string FolderOf(AssetPath source)
        => source.VirtualPath[..Math.Max(source.VirtualPath.LastIndexOf('/'), 0)];

    /// <summary>Shows the model's problems in the Problems pane, filtered to the model.</summary>
    /// <returns>A task that completes when the pane is shown.</returns>
    [RelayCommand]
    private async Task ShowProblemsAsync()
    {
        if (this.window?.Problems is { } problems)
        {
            _ = await problems.ShowFileAsync(this.Document.File.Path.FullPath).ConfigureAwait(true);
        }
    }

    // The panel shows the document's record: its settings as one model file, its node continuity, and the selected
    // mesh's and texture's settings.
    private void ShowRecord()
    {
        var record = this.Model.Record;
        this.HasImportSettings = record is not null;
        this.OnPropertyChanged(nameof(this.IsShowingLastImport));
        if (record is null)
        {
            this.settingsFile = null;
            this.Settings.Continuity = null;
            this.Settings.SelectedMesh = null;
            this.Settings.Show([]);
            return;
        }

        if (this.settingsFile is null)
        {
            this.settingsFile = new ModelImportFile(this.Document.File.Path.FullPath, NameOf(record), FolderOf(this.Source), record.Settings);
            _ = this.AnalyzeAsync(this.settingsFile);
        }
        else
        {
            this.settingsFile.Settings = record.Settings;
        }

        this.Settings.Show([this.settingsFile]);
        this.Settings.Continuity ??= new NodeContinuityGroup(this.Model, this.SelectAnchor);
        this.Settings.Continuity.Refresh();
        this.Settings.SelectedMesh?.Refresh();
    }

    // The source analysis fills the rows that depend on the file: its scenes, materials and what the import does to its size.
    private async Task AnalyzeAsync(ModelImportFile file)
    {
        file.IsAnalyzing = true;
        try
        {
            file.Analysis = await this.services.Analyzer.AnalyzeAsync(this.services.Session.Context, file.SourcePath, this.services.Session.Lifetime).ConfigureAwait(true);
        }
        catch (OperationCanceledException)
        {
            return;
        }
        finally
        {
            file.IsAnalyzing = false;
        }

        if (!this.disposed && ReferenceEquals(this.settingsFile, file))
        {
            this.Settings.Show([file]);
            _ = this.ShowSelectedMeshAsync();
        }
    }

    // A row's edit is one step of the document's history.
    private void OnSettingEdited(object? sender, SettingEditedEventArgs args)
    {
        if (this.disposed || this.settingsFile is not { } file || this.Model.Record is null)
        {
            return;
        }

        this.applyingEdit = true;
        try
        {
            this.Model.EditSettings(file.Settings, args.Label);
        }
        finally
        {
            this.applyingEdit = false;
        }

        this.Settings.Continuity?.Refresh();
        this.Settings.SelectedMesh?.Refresh();
    }

    // An undo, a redo, an edit of a group or a record an import rewrote shows in the panel.
    private void OnModelChanged(object? sender, EventArgs args)
    {
        if (!this.disposed && !this.applyingEdit)
        {
            this.ShowRecord();
        }
    }

    // An anchor of the last import selects its node, as a click in the viewport does.
    private void SelectAnchor(string anchor)
    {
        if (this.Model.Record?.PublishedNodes.FirstOrDefault(node => string.Equals(node.Anchor, anchor, StringComparison.Ordinal)) is not { } node
            || this.services.Scenes.ReferenceOf(this.Document, NodeId.FromValue(node.NodeId)) is not { } reference)
        {
            return;
        }

        this.services.Selection.Publish(this.Document.Id, SceneSelectionContext.ForNodes([reference]), ImportSettingsSelectionSource);
    }

    // The Selected mesh and Selected texture groups follow the primary selected node's geometry.
    private async Task ShowSelectedMeshAsync()
    {
        var read = ++this.meshRead;
        var project = this.services.Session.Context;
        var node = this.services.Selection.GetContext(this.Document.Id).PrimaryNode;
        var structure = this.services.Structures.Find(project, this.Source);
        SelectedMeshGroup? mesh = null;
        IReadOnlyList<string> textures = [];
        if (node is { IsOwned: false } && this.Model.Record is not null
            && structure?.Find(node.Path) is { Geometry: { } geometryUri } structureNode)
        {
            try
            {
                if (await this.services.Surfaces.ReadAsync(project, geometryUri, this.services.Session.Lifetime).ConfigureAwait(true) is { } geometry
                    && this.Model.Record is { } record
                    && ModelGeometryDefaults.AnchorOf(record.SurfaceProvenance, geometry.NativeGeometryKey) is { } anchor)
                {
                    mesh = new SelectedMeshGroup(this.Model, structureNode.Name, anchor, geometry);
                    textures = await this.TexturesOfAsync(geometry).ConfigureAwait(true);
                }
            }
            catch (OperationCanceledException)
            {
                return;
            }
            catch (Exception failure) when (failure is IOException or InvalidDataException or UnauthorizedAccessException)
            {
                // A geometry the last import did not leave readable has no defaults to show; the next cook reads it again.
                this.LogSelectedMeshUnreadable(failure, geometryUri);
            }
        }

        if (!this.disposed && read == this.meshRead)
        {
            this.Settings.SelectedMesh = mesh;
            this.Settings.SelectedTexture.Show(textures);
        }
    }

    // The textures of a geometry's surfaces' materials, by the names the import gives them: the ones the source analysis
    // lists, once it has.
    private async Task<IReadOnlyList<string>> TexturesOfAsync(GeometrySurfaceMetadata geometry)
    {
        var materials = this.materialTextures ??= await this.services.MaterialTextures.ReadTexturesAsync(this.services.Session.Context, this.Source, this.services.Session.Lifetime).ConfigureAwait(true);
        var listed = this.settingsFile?.Facts?.Textures.ToHashSet(StringComparer.Ordinal);
        return
        [
            .. geometry.Surfaces
                .SelectMany(static surface => surface.Bindings)
                .Select(static binding => binding.DefaultMaterialKey)
                .Distinct()
                .SelectMany(material => materials.GetValueOrDefault(material, []))
                .Select(static texture => AssetFileTypes.RemoveSuffix(texture[(texture.LastIndexOf('/') + 1)..]))
                .Where(name => listed is null || listed.Contains(name))
                .Distinct(StringComparer.Ordinal),
        ];
    }

    private void UpdateProblems() => this.ProblemCount = this.services.Problems.ProblemsOf(this.Document).Count;

    private void OnProblemsChanged(object? sender, EventArgs args)
    {
        if (!this.disposed)
        {
            this.UpdateProblems();
        }
    }

    private void OnImportingChanged(object? sender, EventArgs args)
    {
        if (!this.disposed)
        {
            this.ReimportCommand.NotifyCanExecuteChanged();
        }
    }

    // An import rewrites the record, its geometries and its textures: the panel reads them again after any cook, which
    // publishes before its consumers return, so the reads run after it on the editor's thread.
    private Task OnCookCompletedAsync(CookCompletedEventArgs args)
    {
        if (this.owner is { } context)
        {
            context.Post(_ => this.TakeImport(), state: null);
        }
        else
        {
            this.TakeImport();
        }

        return Task.CompletedTask;
    }

    private void TakeImport()
    {
        if (this.disposed)
        {
            return;
        }

        var latest = ModelRecordLookup.TryFind(this.services.Session.Context, this.Source)?.Settings;
        var saved = this.Model.SavedRecord;
        if (latest is not null && saved is not null && latest.ToBytes().AsSpan().SequenceEqual(saved.ToBytes()))
        {
            return;
        }

        this.materialTextures = null;
        this.Model.TakeRecord(latest, keepEdits: this.Document.IsDirty);
        _ = this.ShowSelectedMeshAsync();
    }
}
