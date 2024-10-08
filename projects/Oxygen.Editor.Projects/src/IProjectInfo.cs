// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

using System.Text.Json.Serialization;

public interface IProjectInfo : IEquatable<ProjectInfo?>
{
    string Name { get; set; }

    ProjectCategory Category { get; set; }

    [JsonIgnore]
    string? Location { get; set; }

    string? Thumbnail { get; set; }

    [JsonIgnore]
    DateTime LastUsedOn { get; set; }
}
