// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel.DataAnnotations;
using System.ComponentModel.DataAnnotations.Schema;
using Microsoft.EntityFrameworkCore;

namespace Oxygen.Editor.Data.Models;

/// <summary>
/// Represents a setting for a specific module, identified by a module name and a key.
/// </summary>
/// <remarks>
/// This entity is configured to have a unique index on the combination of <see cref="ModuleName"/> and <see cref="Key"/> to ensure that each module setting is unique.
/// </remarks>
[Table(TableName)]
[Index(nameof(ModuleName), nameof(Key), IsUnique = true)]
public class ModuleSetting
{
    /// <summary>
    /// The name of the database table.
    /// </summary>
    public const string TableName = "Settings";

    /// <summary>
    /// Gets the unique identifier for the module setting.
    /// </summary>
    [Key]
    public int Id { get; init; }

    /// <summary>
    /// Gets the name of the module.
    /// </summary>
    /// <value>
    /// The name of the module. This value is required and must be between 1 and 255 characters in length.
    /// </value>
    [StringLength(255, MinimumLength = 1)]
    public required string ModuleName { get; init; }

    /// <summary>
    /// Gets the key for the module setting.
    /// </summary>
    /// <value>
    /// The key for the module setting. This value is required and must be between 1 and 255 characters in length.
    /// </value>
    [StringLength(255, MinimumLength = 1)]
    public required string Key { get; init; }

    /// <summary>
    /// Gets or sets the JSON value of the module setting.
    /// </summary>
    /// <value>
    /// The JSON value of the module setting. This value is optional and can be up to 2048 characters in length.
    /// </value>
    [StringLength(2048)]
    public string? JsonValue { get; set; }

    /// <summary>
    /// Gets the date and time when the module setting was created.
    /// </summary>
    /// <value>
    /// The date and time when the module setting was created. This value is set to the current UTC date and time by default.
    /// </value>
    public DateTime CreatedAt { get; init; } = DateTime.UtcNow;

    /// <summary>
    /// Gets or sets the date and time when the module setting was last updated.
    /// </summary>
    /// <value>
    /// The date and time when the module setting was last updated. This value is set to the current UTC date and time by default.
    /// </value>
    public DateTime UpdatedAt { get; set; } = DateTime.UtcNow;
}
