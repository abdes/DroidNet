// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Data.Models;

using System.ComponentModel.DataAnnotations;

public class ProjectBrowserState
{
    [Key]
    public int Id { get; set; }

    public string LastSaveLocation { get; set; } = string.Empty;

    public ICollection<RecentlyUsedProject> RecentProjects { get; set; } = [];

    public ICollection<RecentlyUsedTemplate> RecentTemplates { get; set; } = [];
}
