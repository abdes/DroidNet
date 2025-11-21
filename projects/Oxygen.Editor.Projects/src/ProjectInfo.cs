// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Oxygen.Editor.Projects;

/// <summary>
///     Represents the metadata information about a project within the Oxygen Editor.
/// </summary>
/// <remarks>
///     The <see cref="ProjectInfo" /> class implements the <see cref="IProjectInfo" /> interface and provides the metadata
///     information
///     about a project within the Oxygen Editor. This includes properties for the project's name, category, location,
///     thumbnail, last used date
///     and a stable <see cref="Id" /> GUID used to identify the project instance.
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

    /// <summary>
    ///     Initializes a new instance of the <see cref="ProjectInfo" /> class with a newly generated <see cref="Id" />.
    ///     Use this constructor when creating a project at runtime.
    /// </summary>
    /// <param name="name">Project display name.</param>
    /// <param name="category">Project category.</param>
    /// <param name="location">Optional project location.</param>
    /// <param name="thumbnail">Optional thumbnail path.</param>
    [SetsRequiredMembers]
    public ProjectInfo(string name, Category category, string? location = null, string? thumbnail = null)
        : this(Guid.NewGuid(), name, category, location, thumbnail)
    {
    }

    /// <summary>
    ///     Initializes a new instance of the <see cref="ProjectInfo" /> class with the specified <paramref name="id" />.
    ///     This constructor is useful when the caller already has a stable identifier (for example during deserialization).
    /// </summary>
    /// <param name="id">Stable project identifier. Must not be <see cref="Guid.Empty" />.</param>
    /// <param name="name">Project display name.</param>
    /// <param name="category">Project category.</param>
    /// <param name="location">Optional project location.</param>
    /// <param name="thumbnail">Optional thumbnail path.</param>
    [JsonConstructor]
    [SetsRequiredMembers]
    public ProjectInfo(Guid id, string name, Category category, string? location = null, string? thumbnail = null)
    {
        if (id == Guid.Empty)
        {
            throw new ArgumentException("Project Id must be a non-empty GUID.", nameof(id));
        }

        this.Id = id;
        this.Name = name ?? throw new ArgumentNullException(nameof(name));
        this.Category = category;
        this.Location = location;
        this.Thumbnail = thumbnail;
        this.LastUsedOn = DateTime.Now;
    }

    /// <summary>
    ///     Gets the stable identifier for the project. Used for equality and hashing instead of mutable properties.
    ///     This property is required and must not be <see cref="Guid.Empty" />.
    /// </summary>
    public required Guid Id { get; init; }

    /// <inheritdoc />
    public required string Name { get; set; }

    /// <inheritdoc />
    public required Category Category { get; set; }

    /// <inheritdoc />
    public string? Location { get; set; }

    /// <inheritdoc />
    public string? Thumbnail { get; set; }

    /// <inheritdoc />
    public DateTime LastUsedOn { get; set; }

    /// <inheritdoc />
    public bool Equals(ProjectInfo? other)
        => other is not null && (ReferenceEquals(this, other) || this.Id == other.Id);

    /// <inheritdoc />
    public override bool Equals(object? obj) => this.Equals(obj as ProjectInfo);

    /// <inheritdoc />
    public override int GetHashCode() => this.Id.GetHashCode();

    /// <summary>
    ///     Deserializes a JSON string into a <see cref="ProjectInfo" /> object.
    /// </summary>
    /// <param name="json">The JSON string to deserialize.</param>
    /// <returns>The deserialized <see cref="IProjectInfo" /> object.</returns>
    /// <exception cref="JsonException">Thrown when the JSON does not contain a valid non-empty id.</exception>
    internal static IProjectInfo FromJson(string json)
    {
        // First check that the JSON contains a valid, non-empty id property to provide a clear JsonException
        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;
        if (!root.TryGetProperty(nameof(Id), out var idProp))
        {
            throw new JsonException("ProjectInfo JSON is missing required 'Id' property or it is empty.");
        }

        var idText = idProp.ValueKind == JsonValueKind.String ? idProp.GetString() : idProp.ToString();
        if (string.IsNullOrWhiteSpace(idText) || !Guid.TryParse(idText, out var parsedId) || parsedId == Guid.Empty)
        {
            throw new JsonException("ProjectInfo JSON is missing required 'Id' property or it is empty.");
        }

        var obj = JsonSerializer.Deserialize<ProjectInfo>(json, JsonOptions)
            ?? throw new JsonException("Failed to deserialize ProjectInfo from JSON.");
        return obj.Id == Guid.Empty
            ? throw new JsonException("ProjectInfo JSON is missing required 'Id' property or it is empty.")
            : (IProjectInfo)obj;
    }

    /// <summary>
    ///     Serializes a <see cref="IProjectInfo" /> object into a JSON string.
    /// </summary>
    /// <param name="projectInfo">The <see cref="IProjectInfo" /> object to serialize.</param>
    /// <returns>The JSON string representation of the <see cref="IProjectInfo" /> object.</returns>
    internal static string ToJson(IProjectInfo projectInfo) => JsonSerializer.Serialize(projectInfo, JsonOptions);
}
