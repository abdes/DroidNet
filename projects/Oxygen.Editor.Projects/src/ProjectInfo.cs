// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Serialization;

namespace Oxygen.Editor.Projects;

/// <summary>
/// Represents the metadata information about a project within the Oxygen Editor.
/// </summary>
/// <remarks>
/// The <see cref="ProjectInfo"/> class implements the <see cref="IProjectInfo"/> interface and provides the metadata information
/// about a project within the Oxygen Editor. This includes properties for the project's name, category, location, thumbnail, and last used date.
/// </remarks>
public class ProjectInfo : IProjectInfo
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        AllowTrailingCommas = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
        Converters = { new CategoryJsonConverter() },
        WriteIndented = true,
    };

    /// <inheritdoc/>
    public required string Name { get; set; }

    /// <inheritdoc/>
    public required Category Category { get; set; }

    /// <inheritdoc/>
    public string? Location { get; set; }

    /// <inheritdoc/>
    public string? Thumbnail { get; set; }

    /// <inheritdoc/>
    public DateTime LastUsedOn { get; set; } = DateTime.Now;

    /// <inheritdoc/>
    public override bool Equals(object? obj) => this.Equals(obj as ProjectInfo);

    /// <inheritdoc/>
    public bool Equals(ProjectInfo? other)
        => other is not null &&
           (ReferenceEquals(this, other) ||
            (string.Equals(this.Name, other.Name, StringComparison.Ordinal) &&
             string.Equals(this.Location, other.Location, StringComparison.Ordinal)));

    /// <inheritdoc/>
    public override int GetHashCode() => HashCode.Combine(this.Name, this.Location); // TODO: use a GUID for the project info

    /// <summary>
    /// Deserializes a JSON string into a <see cref="ProjectInfo"/> object.
    /// </summary>
    /// <param name="json">The JSON string to deserialize.</param>
    /// <returns>The deserialized <see cref="IProjectInfo"/> object.</returns>
    internal static IProjectInfo? FromJson(string json) => JsonSerializer.Deserialize<ProjectInfo>(json, JsonOptions);

    /// <summary>
    /// Serializes a <see cref="IProjectInfo"/> object into a JSON string.
    /// </summary>
    /// <param name="projectInfo">The <see cref="IProjectInfo"/> object to serialize.</param>
    /// <returns>The JSON string representation of the <see cref="IProjectInfo"/> object.</returns>
    internal static string ToJson(IProjectInfo projectInfo) => JsonSerializer.Serialize(projectInfo, JsonOptions);
}
