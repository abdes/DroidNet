// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel.DataAnnotations;
using System.ComponentModel.DataAnnotations.Schema;
using System.Diagnostics.CodeAnalysis;

namespace Oxygen.Editor.Data.Models;

/// <summary>
/// Represents the usage data of a template, including its location and last used date.
/// </summary>
[Table(TableName)]
public class TemplateUsage
{
    /// <summary>
    /// The name of the database table.
    /// </summary>
    public const string TableName = "TemplatesUsageRecords";

    /// <summary>
    /// Gets the unique identifier for the template usage record.
    /// </summary>
    [Key]
    [SuppressMessage("ReSharper", "UnusedMember.Global", Justification = "used by EF Core")]
    public int Id { get; init; }

    /// <summary>
    /// Gets the location of the template.
    /// </summary>
    [Required]
    [StringLength(1024)]
    [MinLength(1, ErrorMessage = "The template location cannot be empty.")]
    public string Location { get; init; } = string.Empty;

    /// <summary>
    /// Gets or sets the date and time when the template was last used.
    /// </summary>
    [Required]
    public DateTime LastUsedOn { get; set; } = DateTime.Now;

    /// <summary>
    /// Gets or sets the number of times the template has been used.
    /// </summary>
    [Required]
    public int TimesUsed { get; set; }
}
