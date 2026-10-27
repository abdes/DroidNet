// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.Models;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector;

/// <content>
/// A model's facts (model editor 5.4): its source file, where its last import stands and the counts of what it holds,
/// shown on the model's root in its document's tree, in place of the root's component sections, which are the
/// preview's and not the model's. They are read once each time they show. The preview scene's own row shows its scene
/// settings, how the model is looked at.
/// </content>
public sealed partial class SceneInspectorViewModel
{
    private const string UnknownFact = "Unknown";

    // The model whose facts are shown or being read, so a refresh of the same selection reads nothing.
    private AssetPath? modelFactsSource;

    // Grows with each read, so a read that a newer selection replaced is dropped.
    private int modelFactsGeneration;

    /// <summary>Gets the facts of the model, one line each, when its root is selected in its document; empty otherwise.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasModelFacts))]
    public partial IReadOnlyList<ModelFactRow> ModelFactRows { get; private set; } = [];

    /// <summary>Gets the name of the model the pane shows as a whole, such as "Car.glb"; null for any other selection.</summary>
    [ObservableProperty]
    public partial string? ModelTitle { get; private set; }

    /// <summary>Gets a value indicating whether the pane shows a model's facts.</summary>
    public bool HasModelFacts => this.ModelFactRows.Count > 0;

    /// <summary>
    /// Gets a value indicating whether the pane shows the model as a whole, its root selected in its own document: one
    /// card with its name and facts, in place of the node header, its commands, its sections and the read-only notice.
    /// </summary>
    public bool ShowsModelCard => this.ShownModel() is not null;

    // Whether the pane inspects a model's preview scene.
    private bool ShowsModel => this.documents.DocumentOf(this.InspectedScene) is { } document && DocumentScenes.ShowsModel(document);

    /// <summary>Gets the lines the pane shows of a model's facts.</summary>
    /// <param name="facts">The model's facts.</param>
    /// <returns>The source, the last import, then the counts of nodes, meshes, LODs, materials and textures.</returns>
    public static IReadOnlyList<ModelFactRow> RowsOf(ModelFacts facts)
    {
        ArgumentNullException.ThrowIfNull(facts);
        var source = facts.SourceFacts;
        return
        [
            new("Source", facts.Source.ToString()),
            new("Last import", facts.LastImport),
            new("Nodes", Count(source?.NodeCount)),
            new("Meshes", Count(source?.MeshCount)),
            new("LODs", Count(facts.LodCount)),
            new("Materials", Count(source?.Materials.Length)),
            new("Textures", Count(source?.Textures.Length)),
        ];

        static string Count(int? value) => value?.ToString(CultureInfo.CurrentCulture) ?? UnknownFact;
    }

    // The model the selection stands for, in the model's own document: its instance root, selected alone.
    private AssetPath? ShownModel()
        => this.ShowsModel
            && this.items is [var selected]
            && this.SelectedInstancedNode() is null
            && ReferenceEquals(selected, this.InspectedScene?.RootNodes.FirstOrDefault())
            && selected.Components.OfType<SceneInstanceComponent>().FirstOrDefault() is { } instance
                ? instance.Source
                : null;

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The facts are informative: a failed read leaves them out and is logged, and must not end the pane's selection update.")]
    private async void ReadModelFacts()
    {
        var source = this.ShownModel();
        if (Equals(source, this.modelFactsSource))
        {
            return;
        }

        this.modelFactsSource = source;
        var generation = ++this.modelFactsGeneration;
        this.ModelTitle = source?.Name;
        this.ModelFactRows = [];
        if (source is null || this.modelFacts is not { } reader)
        {
            return;
        }

        try
        {
            var facts = await reader.ReadAsync(source).ConfigureAwait(true);
            if (generation == this.modelFactsGeneration && !this.isDisposed)
            {
                this.ModelFactRows = RowsOf(facts);
            }
        }
        catch (Exception ex)
        {
            this.LogModelFactsFailed(source, ex);
        }
    }
}
