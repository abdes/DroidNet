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
    ///     Gets all nodes in the scene (flattened).
    /// </summary>
    [JsonIgnore]
    public IEnumerable<SceneNode> AllNodes => this.RootNodes.SelectMany(r => new[] { r }.Concat(r.Descendants()));

    /// <summary>
    /// Gets or sets editor-only explorer layout persisted alongside the scene file.
    /// This is ignored by the runtime scene graph but included in scene DTOs.
    /// </summary>
    [JsonIgnore]
    public IList<Serialization.ExplorerEntryData>? ExplorerLayout { get; set; }

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
        using (this.SuppressNotifications())
        {
            this.RootNodes.Clear();
            foreach (var nodeData in data.RootNodes)
            {
                var node = SceneNode.CreateAndHydrate(this, nodeData);
                this.RootNodes.Add(node);
            }
        }
    }

    /// <summary>
    ///     Dehydrates this scene to a data transfer object.
    /// </summary>
    /// <returns>A data transfer object containing the current state of this scene.</returns>
    public Serialization.SceneData Dehydrate()
        => new()
        {
            Name = this.Name,
            Id = this.Id,
            RootNodes = [.. this.RootNodes.Select(n => n.Dehydrate())],
            ExplorerLayout = this.ExplorerLayout,
        };
}
