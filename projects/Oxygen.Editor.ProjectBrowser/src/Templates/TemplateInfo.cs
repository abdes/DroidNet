// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;
using DroidNet.Resources.Generator.Localized_a870a544;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;

namespace Oxygen.Editor.ProjectBrowser.Templates;

/// <summary>
/// Represents a project template with its metadata and localization support in the Oxygen Editor.
/// </summary>
/// <remarks>
/// <para>
/// This class implements <see cref="ITemplateInfo"/> and provides JSON serialization support for template persistence.
/// </para>
/// </remarks>
/// <param name="description">The initial description of the template.</param>
/// <param name="name">The initial name of the template.</param>
/// <param name="category">The category this template belongs to.</param>
[method: JsonConstructor]
public class TemplateInfo(string description, string name, Category category) : ITemplateInfo
{
    private string description = description.L();
    private string name = name.L();
    private string? displayName;

    /// <summary>
    /// Gets or sets the localized name of the template.
    /// </summary>
    public required string Name
    {
        get => this.displayName?.L() ?? this.name;
        set => this.name = value.L();
    }

    /// <summary>
    /// Gets or sets the non-localized display name from a V0.1 template descriptor.
    /// </summary>
    public string? DisplayName
    {
        get => this.displayName;
        set => this.displayName = value;
    }

    /// <inheritdoc />
    public string? TemplateId { get; set; }

    /// <inheritdoc />
    public int SchemaVersion { get; set; }

    /// <inheritdoc />
    public string? SourceFolder { get; set; }

    /// <summary>
    /// Gets or sets the authoring mounts declared by the template descriptor.
    /// </summary>
    public IList<ProjectMountPoint> AuthoringMounts { get; set; } = [];

    /// <summary>
    /// Gets or sets the local folder mounts declared by the template descriptor.
    /// </summary>
    public IList<LocalFolderMount> LocalFolderMounts { get; set; } = [];

    /// <summary>
    /// Gets or sets the localized description of the template.
    /// </summary>
    public string Description
    {
        get => this.description;
        set => this.description = value.L();
    }

    /// <summary>
    /// Gets or sets the category of the template.
    /// </summary>
    public Category Category { get; set; } = category;

    /// <summary>
    /// Gets or sets the filesystem path to the template content.
    /// </summary>
    /// <remarks>
    /// This property is ignored during JSON serialization.
    /// </remarks>
    [JsonIgnore]
    public string Location { get; set; } = string.Empty;

    /// <summary>
    /// Gets or sets the path to the template's icon.
    /// </summary>
    public string? Icon { get; set; }

    /// <summary>
    /// Gets or sets the primary preview image path from a V0.1 template descriptor.
    /// </summary>
    public string? Preview { get; set; }

    /// <summary>
    /// Gets or sets the collection of preview image paths for the template.
    /// </summary>
    public IList<string> PreviewImages { get; set; } = [];

    /// <summary>
    /// Gets or sets the starter scene declared by the template.
    /// </summary>
    public TemplateStarterSceneInfo? StarterScene { get; set; }

    /// <summary>
    /// Gets or sets the starter content assets declared by the template.
    /// </summary>
    public IList<TemplateStarterContentInfo> StarterContent { get; set; } = [];

    /// <summary>
    /// Gets or sets the last time this template was used.
    /// </summary>
    /// <remarks>
    /// This property is ignored during JSON serialization and defaults to the current time.
    /// </remarks>
    [JsonIgnore]
    public DateTime LastUsedOn { get; set; } = DateTime.Now;
}

/// <summary>
/// Starter scene metadata declared by a project template.
/// </summary>
public sealed record TemplateStarterSceneInfo(Uri AssetUri, string? RelativePath, bool OpenOnCreate);

/// <summary>
/// Starter asset metadata declared by a project template.
/// </summary>
public sealed record TemplateStarterContentInfo(Uri AssetUri, string? RelativePath, string? Kind);
