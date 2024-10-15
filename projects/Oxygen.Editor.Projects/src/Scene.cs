// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

using System.Text.Json.Serialization;

[method: JsonConstructor]
public class Scene(string name) : NamedItem(name)
{
    [JsonIgnore]
    public Project? Project { get; init; }

    [System.Diagnostics.CodeAnalysis.SuppressMessage(
        "Design",
        "MA0016:Prefer using collection abstraction instead of implementation",
        Justification = "need AddRange from List")]
    [JsonInclude]
    public List<Entity> Entities { get; private set; } = [];
}
