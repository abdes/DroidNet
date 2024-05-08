// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Projects;

public class ProjectInfo : IProjectInfo
{
    public required string Name { get; set; }

    public required ProjectCategory Category { get; set; }

    public string? Location { get; set; }

    public string? Thumbnail { get; set; }

    public DateTime LastUsedOn { get; set; } = DateTime.Now;
}
