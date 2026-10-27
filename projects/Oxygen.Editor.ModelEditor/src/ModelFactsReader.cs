// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using System.Reactive.Threading.Tasks;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Models;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ModelEditor;

/// <summary>
/// Reads a model's facts: the counts of its source file from the source analysis, its last import from its scene
/// product's cook state, and its LODs from the geometries its last import produced.
/// </summary>
/// <remarks>
/// The counts and the LODs change only when the model is imported again: they are read once per model, which runs the
/// source analysis, and kept until the model's imported structure is read anew, as after a reimport. The last import
/// is read each time, from the project's assets.
/// </remarks>
internal sealed class ModelFactsReader : IModelFacts, IDisposable
{
    private readonly IProjectSession session;
    private readonly IAssetFeed assets;
    private readonly IModelSourceAnalyzer analyzer;
    private readonly ISceneStructures structures;
    private readonly IGeometrySurfaceProvider surfaces;
    private readonly Lock gate = new();
    private readonly Dictionary<AssetPath, KeptContents> kept = [];

    /// <summary>Initializes a new instance of the <see cref="ModelFactsReader"/> class.</summary>
    /// <param name="session">The project session.</param>
    /// <param name="assets">The project's assets.</param>
    /// <param name="analyzer">Reads what a source file holds.</param>
    /// <param name="structures">What an instance of a source contains; a new structure of a model forgets its facts.</param>
    /// <param name="surfaces">The imported geometries' surfaces and LODs.</param>
    public ModelFactsReader(IProjectSession session, IAssetFeed assets, IModelSourceAnalyzer analyzer, ISceneStructures structures, IGeometrySurfaceProvider surfaces)
    {
        this.session = session;
        this.assets = assets;
        this.analyzer = analyzer;
        this.structures = structures;
        this.surfaces = surfaces;
        this.structures.Changed += this.OnStructuresChanged;
    }

    /// <inheritdoc />
    public async Task<ModelFacts> ReadAsync(AssetPath source, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(source);
        var entries = await this.assets.Items.FirstAsync().ToTask(cancellationToken).ConfigureAwait(false);
        var contents = await this.ContentsOf(this.session.Context, source).WaitAsync(cancellationToken).ConfigureAwait(false);
        return new ModelFacts
        {
            Source = source,
            LastImport = ModelImportStatus.TextOf(ModelImportStatus.Of(source, entries)),
            SourceFacts = contents.SourceFacts,
            LodCount = contents.LodCount,
        };
    }

    /// <inheritdoc />
    public void Dispose() => this.structures.Changed -= this.OnStructuresChanged;

    // The model's counts and LODs, read once and shared by every reader until the model's structure changes; a read that
    // failed is read again.
    private Task<ModelContents> ContentsOf(ProjectContext project, AssetPath source)
    {
        lock (this.gate)
        {
            if (this.kept.TryGetValue(source, out var contents) && ReferenceEquals(contents.Project, project) && !contents.Read.IsFaulted && !contents.Read.IsCanceled)
            {
                return contents.Read;
            }

            var read = this.ReadContentsAsync(project, source);
            this.kept[source] = new KeptContents(project, this.structures.Find(project, source), read);
            return read;
        }
    }

    private async Task<ModelContents> ReadContentsAsync(ProjectContext project, AssetPath source)
    {
        var analysis = project.Find(source) is { } location
            ? await this.analyzer.AnalyzeAsync(project, location.File.FullPath, CancellationToken.None).ConfigureAwait(false)
            : null;
        return new ModelContents(
            analysis is { Complete: true } ? analysis.Facts : null,
            await this.ReadLodCountAsync(project, source).ConfigureAwait(false));
    }

    // The most LODs of the geometries an instance of the model renders; null when none of them can be read.
    private async Task<int?> ReadLodCountAsync(ProjectContext project, AssetPath source)
    {
        int? most = null;
        var geometries = this.structures.Find(project, source)?.Nodes.Select(static node => node.Geometry).OfType<AssetPath>().Distinct() ?? [];
        foreach (var geometry in geometries)
        {
            if (await this.surfaces.ReadAsync(project, geometry).ConfigureAwait(false) is { } metadata)
            {
                most = Math.Max(most ?? 0, metadata.LodCount);
            }
        }

        return most;
    }

    // A model whose imported structure was read anew, as after a reimport, has its facts read again.
    private void OnStructuresChanged(object? sender, EventArgs args)
    {
        lock (this.gate)
        {
            foreach (var (source, contents) in this.kept.ToArray())
            {
                if (!ReferenceEquals(this.structures.Find(contents.Project, source), contents.Structure))
                {
                    _ = this.kept.Remove(source);
                }
            }
        }
    }

    private sealed record ModelContents(ModelSourceFacts? SourceFacts, int? LodCount);

    private sealed record KeptContents(ProjectContext Project, SceneStructure? Structure, Task<ModelContents> Read);
}
