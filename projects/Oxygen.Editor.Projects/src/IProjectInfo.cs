// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Projects;

/// <summary>
///     Represents the metadata information about a project within the Oxygen Editor.
/// </summary>
/// <remarks>
///     The <see cref="IProjectInfo" /> interface defines the structure for project metadata, including properties for the
///     project's
///     name, category, location, thumbnail, last used date, and a stable identifier.
/// </remarks>
public interface IProjectInfo
{
    /// <summary>
    ///     Gets the stable GUID identifier for the project. Used for persistent identity and hashing.
    /// </summary>
    public Guid Id { get; }

    /// <summary>
    ///     Gets or sets the project manifest schema version.
    /// </summary>
    public int SchemaVersion { get; set; }

    /// <summary>
    ///     Gets or sets the name of the project.
    /// </summary>
    /// <value>
    ///     A <see cref="string" /> representing the name of the project.
    /// </value>
    public string Name { get; set; }

    /// <summary>
    ///     Gets or sets the category of the project.
    /// </summary>
    /// <value>
    ///     A <see cref="Category" /> representing the category of the project.
    /// </value>
    public Category Category { get; set; }

    /// <summary>
    ///     Gets or sets the location of the project.
    /// </summary>
    /// <value>
    ///     A <see cref="string" /> representing the location of the project. This property is ignored during JSON
    ///     serialization.
    /// </value>
    [JsonIgnore]
    public string? Location { get; set; }

    /// <summary>
    ///     Gets or sets the thumbnail image path of the project.
    /// </summary>
    /// <value>
    ///     A <see cref="string" /> representing the path to the thumbnail image of the project.
    /// </value>
    public string? Thumbnail { get; set; }

    /// <summary>
    ///     Gets the mounts the project declares: source folders, cooked folders and pak files, each with a path
    ///     relative to the project root or none for a placeholder. A <c>.mounts.json</c> binds them on each machine.
    /// </summary>
    public IList<ProjectMount> Mounts { get; }

    /// <summary>Gets the output folders the Content Explorer shows; view state, never mounts.</summary>
    public IList<OutputFolderView> OutputFolderViews { get; }

    /// <summary>Gets cooked-content source groups in mount order, from lowest to highest priority.</summary>
    public IList<CookedContentSource> CookedContentOrder { get; }

    /// <summary>
    ///     Gets or sets the last used date of the project.
    /// </summary>
    /// <value>
    ///     A <see cref="DateTime" /> representing the last time the project was used. This property is ignored during JSON
    ///     serialization.
    /// </value>
    [JsonIgnore]
    public DateTime LastUsedOn { get; set; }

    /// <summary>
    ///     Gets or sets the stable identity of the project's configured default scene, or <see langword="null"/>
    ///     when no default is configured.
    /// </summary>
    /// <remarks>
    ///     The default scene is the single scene loaded on fresh project activation. It is resolved by stable ID
    ///     (surviving rename) and must reference one of the project's discovered scenes; a missing or invalid value
    ///     requires an explicit choice rather than falling back to last-opened or first-listed.
    /// </remarks>
    public SceneId? DefaultSceneId { get; set; }

    /// <summary>Gets or sets the project's settings.</summary>
    public ProjectSettings Settings { get; set; }
}
