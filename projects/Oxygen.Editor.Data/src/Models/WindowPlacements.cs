// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel.DataAnnotations;
using System.ComponentModel.DataAnnotations.Schema;
using Microsoft.EntityFrameworkCore;
using Microsoft.EntityFrameworkCore.Metadata.Internal;

namespace Oxygen.Editor.Data.Models;

/// <summary>
///     Represents a record of window placement information for a specific key.
/// </summary>
[Table(TableName)]
[Index(nameof(PlacementKey), IsUnique = true)]
public class WindowPlacements
{
    /// <summary>
    ///     The name of the database table.
    /// </summary>
    public const string TableName = "WindowPlacements";

    /// <summary>
    ///     Gets the unique identifier for the window placement record.
    /// </summary>
    [Key]
    public int Id { get; init; }

    /// <summary>
    ///     Gets or sets the unique key identifying the window placement.
    /// </summary>
    [Required]
    [StringLength(255)]
    [MinLength(1, ErrorMessage = "the placement key cannot be empty")]
    public string PlacementKey { get; set; } = string.Empty;

    /// <summary>
    ///     Gets or sets the serialized data representing the window placement.
    /// </summary>
    [Required]
    [StringLength(255)]
    [MinLength(1, ErrorMessage = "the placement data cannot be empty")]
    public string PlacementData { get; set; } = string.Empty;
}
