// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Text.Json.Serialization;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World;

/// <summary>
///     Represents a scene: its node hierarchy and its scene-level settings.
/// </summary>
/// <remarks>
///     A scene does not know where it is stored or which project opened it; whoever holds it knows that. The class
///     also provides methods for JSON serialization and deserialization.
/// </remarks>
public partial class Scene : GameObject, IPersistent<Serialization.SceneData>
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="Scene"/> class.
    /// </summary>
    public Scene()
    {
        this.Name = "Untitled Scene"; // Initialize required property
    }

    /// <summary>
    ///     Gets the identity of the scene, authored in its file.
    /// </summary>
    public SceneId Id { get; init; } = SceneId.New();

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
    public Serialization.SceneReferencesData References { get; private set; } = NormalizeReferences(references: null);

    /// <summary>
    /// Gets the camera node the scene starts viewing through, a node of the scene or one an instance brings in, or
    /// <see langword="null"/> for the first camera in hierarchy order.
    /// </summary>
    public NodeReference? StartingCamera { get; private set; }

    /// <summary>
    /// Gets the input mapping contexts that are active when the scene starts, in order.
    /// </summary>
    public IReadOnlyList<Serialization.StartingInputContextData> StartingInputContexts { get; private set; } = NormalizeContexts(contexts: null);

    /// <summary>
    /// Gets the camera node the scene starts viewing through: the authored starting camera, else the first camera in
    /// hierarchy order.
    /// </summary>
    /// <remarks>
    /// An authored starting camera that is missing or no longer a camera is returned as <see langword="null"/>;
    /// the cook reports it. One an instance brings in is returned while its instance root is in the scene: whether the
    /// node it names is a camera is known from the instanced source, which the cook checks.
    /// </remarks>
    [JsonIgnore]
    public NodeReference? EffectiveStartingCamera
    {
        get
        {
            if (this.StartingCamera is not { } authored)
            {
                return this.AllNodes.FirstOrDefault(static node => node.Components.OfType<CameraComponent>().Any()) is { } first
                    ? NodeReference.To(first.Id)
                    : null;
            }

            var node = this.AllNodes.FirstOrDefault(node => node.Id == authored.Node);
            var resolves = authored.IsOwned
                ? node?.Components.OfType<CameraComponent>().Any() == true
                : node?.Components.OfType<SceneInstanceComponent>().Any() == true;
            return resolves ? authored : null;
        }
    }

    /// <summary>
    ///     Gets all nodes in the scene (flattened).
    /// </summary>
    [JsonIgnore]
    public IEnumerable<SceneNode> AllNodes => this.RootNodes.SelectMany(r => new[] { r }.Concat(r.Descendants()));

    /// <summary>
    ///     Creates and hydrates a <see cref="Scene"/> instance from the specified DTO.
    /// </summary>
    /// <param name="data">DTO containing scene data.</param>
    /// <returns>A hydrated <see cref="Scene"/> instance.</returns>
    /// <exception cref="System.Text.Json.JsonException">A node lacks or repeats a required component.</exception>
    /// <exception cref="ArgumentException">The scene model rejects a value, such as an empty name or invalid lights.</exception>
    public static Scene CreateAndHydrate(Serialization.SceneData data)
    {
        ArgumentNullException.ThrowIfNull(data);
        var scene = new Scene { Name = data.Name, Id = data.Id };
        scene.Hydrate(data);
        return scene;
    }

    /// <summary>
    ///     Hydrates this scene instance from the specified data transfer object.
    ///     This instance method assumes the instance's required properties (Name/Id)
    ///     were set by the factory. It only restores the scene contents.
    /// </summary>
    /// <param name="data">DTO containing scene data.</param>
    /// <exception cref="System.Text.Json.JsonException">A node lacks or repeats a required component.</exception>
    /// <exception cref="ArgumentException">The scene model rejects a value, such as an empty name or invalid lights.</exception>
    public void Hydrate(Serialization.SceneData data)
    {
        Serialization.SceneSerializer.EnsureFactories();
        using (this.SuppressNotifications())
        {
            // Build every node before replacing any, so data the model rejects leaves the scene as it was. Building
            // checks each value and each light's rules; the scene's light ownership is checked once all are known.
            var nodes = data.RootNodes.Select(nodeData => SceneNode.CreateAndHydrate(this, nodeData)).ToList();
            if (LightValidation.ValidateScene(data) is { } error)
            {
                throw new ArgumentException(error, nameof(data));
            }

            this.RootNodes.Clear();
            foreach (var node in nodes)
            {
                this.RootNodes.Add(node);
            }

            this.Environment = NormalizeEnvironment(data.Environment);
            this.References = NormalizeReferences(data.References);
            this.StartingCamera = data.StartingCamera;
            this.StartingInputContexts = NormalizeContexts(data.StartingInputContexts);
        }
    }

    /// <summary>
    /// Gets whether a reference can name a node of this scene: a node of the scene itself, or a node below one of its
    /// instance roots.
    /// </summary>
    /// <param name="reference">The reference.</param>
    /// <returns>
    /// Whether the node, or the instance root the path starts from, is in the scene. Whether the instanced source holds
    /// the path is known from that source's structure, not from the scene.
    /// </returns>
    public bool Holds(NodeReference reference)
    {
        ArgumentNullException.ThrowIfNull(reference);
        var node = this.AllNodes.FirstOrDefault(node => node.Id == reference.Node);
        return node is not null && (reference.IsOwned || node.Components.OfType<SceneInstanceComponent>().Any());
    }

    /// <summary>
    ///     Dehydrates this scene to a data transfer object.
    /// </summary>
    /// <returns>A data transfer object containing the current state of this scene.</returns>
    public Serialization.SceneData Dehydrate()
        => LightValidation.ValidateScene(this) is { } error
            ? throw new InvalidOperationException(error)
            : new()
            {
                Name = this.Name,
                Id = this.Id,
                RootNodes = [.. this.RootNodes.Select(n => n.Dehydrate())],
                Environment = NormalizeEnvironment(this.Environment),
                References = this.References.IsEmpty ? null : NormalizeReferences(this.References),
                StartingCamera = this.StartingCamera,
                StartingInputContexts = this.StartingInputContexts.Count == 0 ? null : [.. this.StartingInputContexts],
            };

    /// <summary>
    /// Replaces scene-level environment authoring data.
    /// </summary>
    /// <param name="environment">The new environment data.</param>
    public void SetEnvironment(Serialization.SceneEnvironmentData environment)
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
    public void SetReferences(Serialization.SceneReferencesData references)
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

    /// <summary>
    /// Replaces the camera node the scene starts viewing through.
    /// </summary>
    /// <param name="camera">The camera node, or <see langword="null"/> for the first camera in hierarchy order.</param>
    public void SetStartingCamera(NodeReference? camera)
    {
        var normalized = camera;
        if (this.StartingCamera != normalized)
        {
            this.OnPropertyChanging(nameof(this.StartingCamera));
            this.StartingCamera = normalized;
            this.OnPropertyChanged(nameof(this.StartingCamera));
        }
    }

    /// <summary>
    /// Replaces the input mapping contexts that are active when the scene starts.
    /// </summary>
    /// <param name="contexts">The contexts, in order.</param>
    public void SetStartingInputContexts(IEnumerable<Serialization.StartingInputContextData> contexts)
    {
        ArgumentNullException.ThrowIfNull(contexts);
        var normalized = NormalizeContexts(contexts);
        if (!this.StartingInputContexts.SequenceEqual(normalized))
        {
            this.OnPropertyChanging(nameof(this.StartingInputContexts));
            this.StartingInputContexts = normalized;
            this.OnPropertyChanged(nameof(this.StartingInputContexts));
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
            SkySphere = environment.SkySphere ?? new(),
            SkyLight = environment.SkyLight ?? new(),

            // The former color was only required to be finite; the background is display-only SDR.
            Background = environment.LegacyBackgroundColor is { } legacy
                ? new() { Enabled = true, ColorRgb = System.Numerics.Vector3.Clamp(legacy, System.Numerics.Vector3.Zero, System.Numerics.Vector3.One) }
                : environment.Background ?? new(),
            LegacyBackgroundColor = null,
        };
    }

    private static Serialization.SceneReferencesData NormalizeReferences(Serialization.SceneReferencesData? references)
    {
        references ??= new();

        ArgumentNullException.ThrowIfNull(references.Scripts, nameof(references));
        ArgumentNullException.ThrowIfNull(references.InputActions, nameof(references));
        ArgumentNullException.ThrowIfNull(references.PhysicsSidecars, nameof(references));
        ArgumentNullException.ThrowIfNull(references.ExtraAssets, nameof(references));

        return new()
        {
            Scripts = new List<AssetPath>(references.Scripts).AsReadOnly(),
            InputActions = new List<AssetPath>(references.InputActions).AsReadOnly(),
            PhysicsSidecars = new List<AssetPath>(references.PhysicsSidecars).AsReadOnly(),
            ExtraAssets = new List<AssetPath>(references.ExtraAssets).AsReadOnly(),
        };
    }

    private static ReadOnlyCollection<Serialization.StartingInputContextData> NormalizeContexts(IEnumerable<Serialization.StartingInputContextData>? contexts)
        => new List<Serialization.StartingInputContextData>(contexts ?? []).AsReadOnly();

    private static bool ReferencesEqual(Serialization.SceneReferencesData left, Serialization.SceneReferencesData right)
        => left.Scripts.SequenceEqual(right.Scripts)
            && left.InputActions.SequenceEqual(right.InputActions)
            && left.PhysicsSidecars.SequenceEqual(right.PhysicsSidecars)
            && left.ExtraAssets.SequenceEqual(right.ExtraAssets);
}
