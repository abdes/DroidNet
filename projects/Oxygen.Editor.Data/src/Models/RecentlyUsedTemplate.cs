// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Data.Models;

using System.ComponentModel.DataAnnotations;
using Microsoft.EntityFrameworkCore;

[Index(nameof(Location))]
public class RecentlyUsedTemplate
{
    [Key]
    public int Id { get; set; }

    [Required]
    public string? Location { get; set; }

    [Required]
    public DateTime LastUsedOn { get; set; } = DateTime.Now;

    [Required]
    public int ProjectBrowserStateId { get; set; }
}
