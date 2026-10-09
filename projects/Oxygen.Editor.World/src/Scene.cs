// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Text.Json.Serialization;

namespace Oxygen.Editor.World;

/// <summary>
///     Represents a scene in a game project.
/// </summary>
/// <remarks>
///     The <see cref="Scene" /> class represents a scene within a game project. It includes properties for the project
///     that owns the scene and the entities within the scene. The class also provides methods for JSON serialization and
///     deserialization.
/// </remarks>
public partial class Scene : GameObject, IPersistent<Serialization.SceneData>
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="Scene"/> class.
    /// </summary>
    /// <param name="project">The owner <see cref="IProject"/>.</param>
    public Scene(IProject project)
    {
        this.Project = project;
        this.Name = "Untitled Scene"; // Initialize required property
    }

    /// <summary>
    ///     Gets the project that owns the scene.
    /// </summary>
    [JsonIgnore]
    public IProject Project { get; init; }

    /// <summary>
    ///     Gets the list of root entities within the scene.
    /// </summary>
    public ObservableCollection<SceneNode> RootNodes { get; init; } = [];

    /// <summary>
    /// Gets scene-level environment authoring data.
    /// </summary>
    public Serialization.SceneEnvironmentData Environment { get; private set; } = new();

    /// <summary>
    /// Gets the scene-level native asset references with read-only collections.
    /// </summary>
    public Serialization.SceneReferencesData References { get; private set; } = NormalizeReferences(null);

    /// <summary>
    ///     Gets all nodes in the scene (flattened).
    /// </summary>
    [JsonIgnore]
    public IEnumerable<SceneNode> AllNodes => this.RootNodes.SelectMany(r => new[] { r }.Concat(r.Descendants()));

    /// <summary>
    /// Gets editor-only explorer layout persisted alongside the scene file.
    /// This is ignored by the runtime scene graph but included in scene DTOs.
    /// </summary>
    [JsonIgnore]
    public IList<Serialization.ExplorerEntryData>? ExplorerLayout { get; private set; }

    /// <summary>
    ///     Creates and hydrates a <see cref="Scene"/> instance from the specified DTO.
    /// </summary>
    /// <param name="project">Owner project for the new scene.</param>
    /// <param name="data">DTO containing scene data.</param>
    /// <returns>A hydrated <see cref="Scene"/> instance.</returns>
    public static Scene CreateAndHydrate(IProject project, Serialization.SceneData data)
    {
        var scene = new Scene(project) { Name = data.Name, Id = data.Id };
        scene.Hydrate(data);
        return scene;
    }

    /// <summary>
    ///     Hydrates this scene instance from the specified data transfer object.
    ///     This instance method assumes the instance's required properties (Name/Id)
    ///     were set by the factory. It only restores the scene contents.
    /// </summary>
    /// <param name="data">DTO containing scene data.</param>
    public void Hydrate(Serialization.SceneData data)
    {
        if (LightValidation.ValidateScene(data) is { } error)
        {
            throw new ArgumentException(error, nameof(data));
        }

        using (this.SuppressNotifications())
        {
            this.RootNodes.Clear();
            foreach (var nodeData in data.RootNodes)
            {
                var node = SceneNode.CreateAndHydrate(this, nodeData);
                this.RootNodes.Add(node);
            }

            this.Environment = NormalizeEnvironment(data.Environment);
            this.References = NormalizeReferences(data.References);
        }
    }

    /// <summary>
    ///     Dehydrates this scene to a data transfer object.
    /// </summary>
    /// <returns>A data transfer object containing the current state of this scene.</returns>
    public Serialization.SceneData Dehydrate()
    {
        if (LightValidation.ValidateScene(this) is { } error)
        {
            throw new InvalidOperationException(error);
        }

        return new()
        {
            Name = this.Name,
            Id = this.Id,
            RootNodes = [.. this.RootNodes.Select(n => n.Dehydrate())],
            Environment = NormalizeEnvironment(this.Environment),
            References = this.References.IsEmpty ? null : NormalizeReferences(this.References),
            ExplorerLayout = this.ExplorerLayout,
        };
    }

    /// <summary>
    /// Replaces editor-only Scene Explorer layout data.
    /// </summary>
    /// <param name="explorerLayout">The new explorer layout, or <see langword="null"/> to clear it.</param>
    public void SetExplorerLayout(IList<Serialization.ExplorerEntryData>? explorerLayout)
        => this.ExplorerLayout = explorerLayout;

    /// <summary>
    /// Replaces scene-level environment authoring data.
    /// </summary>
    /// <param name="environment">The new environment data.</param>
    internal void SetEnvironment(Serialization.SceneEnvironmentData environment)
    {
        ArgumentNullException.ThrowIfNull(environment);
        var normalized = NormalizeEnvironment(environment);
        if (this.Environment != normalized)
        {
            this.OnPropertyChanging(nameof(this.Environment));
            this.Environment = normalized;
            this.OnPropertyChanged(nameof(this.Environment));
        }
    }

    /// <summary>
    /// Replaces scene-level asset references.
    /// </summary>
    /// <param name="references">The new reference data.</param>
    internal void SetReferences(Serialization.SceneReferencesData references)
    {
        ArgumentNullException.ThrowIfNull(references);
        var normalized = NormalizeReferences(references);
        if (!ReferencesEqual(this.References, normalized))
        {
            this.OnPropertyChanging(nameof(this.References));
            this.References = normalized;
            this.OnPropertyChanged(nameof(this.References));
        }
    }

    private static Serialization.SceneEnvironmentData NormalizeEnvironment(Serialization.SceneEnvironmentData? environment)
    {
        environment ??= new();
        return environment with
        {
            SkyAtmosphere = environment.SkyAtmosphere ?? new(),
            PostProcess = environment.PostProcess ?? new(),
            Fog = environment.Fog ?? new(),
        };
    }

    private static Serialization.SceneReferencesData NormalizeReferences(Serialization.SceneReferencesData? references)
    {
        references ??= new();

        ArgumentNullException.ThrowIfNull(references.Scripts);
        ArgumentNullException.ThrowIfNull(references.InputActions);
        ArgumentNullException.ThrowIfNull(references.InputMappingContexts);
        ArgumentNullException.ThrowIfNull(references.PhysicsSidecars);
        ArgumentNullException.ThrowIfNull(references.ExtraAssets);

        return new()
        {
            Scripts = new List<Uri>(references.Scripts).AsReadOnly(),
            InputActions = new List<Uri>(references.InputActions).AsReadOnly(),
            InputMappingContexts = new List<Uri>(references.InputMappingContexts).AsReadOnly(),
            PhysicsSidecars = new List<Uri>(references.PhysicsSidecars).AsReadOnly(),
            ExtraAssets = new List<string>(references.ExtraAssets).AsReadOnly(),
        };
    }

    private static bool ReferencesEqual(Serialization.SceneReferencesData left, Serialization.SceneReferencesData right)
        => left.Scripts.SequenceEqual(right.Scripts)
            && left.InputActions.SequenceEqual(right.InputActions)
            && left.InputMappingContexts.SequenceEqual(right.InputMappingContexts)
            && left.PhysicsSidecars.SequenceEqual(right.PhysicsSidecars)
            && left.ExtraAssets.SequenceEqual(right.ExtraAssets, StringComparer.Ordinal);
}
