// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace Oxygen.Editor.World;

/// <summary>Identifies the owner of a cooked-content source group.</summary>
[JsonConverter(typeof(JsonStringEnumConverter<CookedContentSourceKind>))]
public enum CookedContentSourceKind
{
    /// <summary>All cooked roots owned and published by the project.</summary>
    ProjectOutput,

    /// <summary>A declared local folder containing a cooked library.</summary>
    LocalFolder,
}

/// <summary>Identifies one group in the project's cooked-content mount order.</summary>
/// <param name="Kind">Project-owned output or a declared local folder.</param>
/// <param name="Name">The declared folder name; omitted for project output.</param>
public sealed record CookedContentSource(CookedContentSourceKind Kind, string? Name = null);
