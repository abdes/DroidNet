// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

public class ProjectInfo : IProjectInfo
{
    public required string Name { get; set; }

    public required ProjectCategory Category { get; set; }

    public string? Location { get; set; }

    public string? Thumbnail { get; set; }

    public DateTime LastUsedOn { get; set; } = DateTime.Now;

    public override bool Equals(object? obj) => this.Equals(obj as ProjectInfo);

    public bool Equals(ProjectInfo? other)
    {
        if (other is null)
        {
            return false;
        }

        if (ReferenceEquals(this, other))
        {
            return true;
        }

        return string.Equals(this.Name, other.Name, StringComparison.Ordinal) &&
               string.Equals(this.Location, other.Location, StringComparison.Ordinal);
    }

    public override int GetHashCode() => HashCode.Combine(this.Name, this.Location);
}
