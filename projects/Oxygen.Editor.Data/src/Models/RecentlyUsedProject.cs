// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Data.Models;

using System.ComponentModel.DataAnnotations;
using Microsoft.EntityFrameworkCore;

[Index(nameof(Location))]
public class RecentlyUsedProject(int id, string? location, DateTime lastUsedOn, int projectBrowserStateId = 0)
{
    public RecentlyUsedProject(int id, string? location, int projectBrowserStateId = 0)
        : this(id, location, DateTime.Now, projectBrowserStateId)
    {
    }

    [Key]
    public int Id { get; set; } = id;

    [Required]
    public string? Location { get; set; } = location;

    [Required]
    public DateTime LastUsedOn { get; set; } = lastUsedOn;

    [Required]
    public int ProjectBrowserStateId { get; set; } = projectBrowserStateId;
}
