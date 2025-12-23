// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace Oxygen.Editor.World;

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
    ///     Gets or sets the project authoring mount points.
    /// </summary>
    /// <remarks>
    ///     Mount points are persisted in the project manifest (<c>Project.oxy</c>) and provide the authoritative mapping
    ///     from a mount point name (virtual root segment) to a project-relative authoring folder.
    /// </remarks>
    public IList<ProjectMountPoint> MountPoints { get; set; }

    /// <summary>
    ///     Gets or sets the last used date of the project.
    /// </summary>
    /// <value>
    ///     A <see cref="DateTime" /> representing the last time the project was used. This property is ignored during JSON
    ///     serialization.
    /// </value>
    [JsonIgnore]
    public DateTime LastUsedOn { get; set; }
}
