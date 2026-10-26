// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Cooking;

/// <summary>One place an issue applies to: what it is called, where it is, and what kind of asset it is when known.</summary>
/// <param name="Path">Where the place is: the object's path inside its source file, or the scope when the diagnostic has no path.</param>
/// <param name="Name">The name the source gives the object, when it has one.</param>
/// <param name="Kind">The asset type, named as the Assets view names it, when the diagnostic identifies it.</param>
public sealed record CookingIssuePlace(string Path, string? Name, string? Kind)
{
    /// <summary>Gets the Name column: the name the source gives the object, empty when it has none.</summary>
    public string Display => this.Name ?? string.Empty;

    /// <summary>Gets the Path column: where the object is.</summary>
    public string Detail => this.Path;

    /// <summary>Gets the kind text, empty when unknown.</summary>
    public string KindText => this.Kind ?? string.Empty;

    /// <summary>Gets the place as one line of plain text for copying.</summary>
    public string Line => string.Join(" · ", new[] { this.Path, this.Name, this.Kind }.Where(static part => !string.IsNullOrWhiteSpace(part)));

    /// <summary>Reads the place of a diagnostic.</summary>
    /// <param name="diagnostic">The diagnostic.</param>
    /// <param name="scope">The asset or scene it applies to, used when it has no object path.</param>
    /// <returns>The place.</returns>
    internal static CookingIssuePlace Of(DiagnosticRecord diagnostic, string scope)
    {
        var path = string.IsNullOrWhiteSpace(diagnostic.TechnicalMessage) ? scope : diagnostic.TechnicalMessage;
        return new(path, diagnostic.AffectedEntity?.SourceObjectName, KindOf(diagnostic.AffectedEntity?.SourceObjectPath ?? diagnostic.TechnicalMessage));
    }

    // A source object's path starts with the collection it belongs to, as /materials/3 does in a glTF file.
    private static string? KindOf(string? objectPath)
        => objectPath?.TrimStart('/').Split('/', 2)[0] switch
        {
            "materials" => "Material",
            "meshes" => "Geometry",
            "textures" or "images" => "Texture",
            "scenes" => "Scene",
            _ => null,
        };
}
