// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel.DataAnnotations;
using System.ComponentModel.DataAnnotations.Schema;
using System.Diagnostics.CodeAnalysis;

namespace Oxygen.Editor.Data.Models;

/// <summary>
/// Represents the usage data of a project, including its location, name, last used date, last opened scene, and content browser state.
/// </summary>
[Table(TableName)]
public class ProjectUsage
{
    /// <summary>
    /// The name of the database table.
    /// </summary>
    public const string TableName = "ProjectsUsage";

    /// <summary>
    /// Gets the unique identifier for the project usage record.
    /// </summary>
    [Key]
    [SuppressMessage("ReSharper", "UnusedMember.Global", Justification = "used by EF Core")]
    public int Id { get; init; }

    /// <summary>
    /// Gets or sets the name of the project.
    /// </summary>
    [Required]
    [StringLength(255)]
    [MinLength(1, ErrorMessage = "the project name cannot be empty")]
    public string Name { get; set; } = string.Empty;

    /// <summary>
    /// Gets or sets the location of the project.
    /// </summary>
    [Required]
    [StringLength(2048)]
    [MinLength(1, ErrorMessage = "the project location cannot be empty")]
    public string Location { get; set; } = string.Empty;

    /// <summary>
    /// Gets or sets the number of times the project has been opened.
    /// </summary>
    [Required]
    public int TimesOpened { get; set; }

    /// <summary>
    /// Gets or sets the date and time when the project was last used.
    /// </summary>
    [Required]
    public DateTime LastUsedOn { get; set; } = DateTime.Now;

    /// <summary>
    /// Gets or sets the name of the last opened scene in the project.
    /// </summary>
    [Required]
    [StringLength(255)]
    public string LastOpenedScene { get; set; } = string.Empty;

    /// <summary>
    /// Gets or sets the state of the content browser for the project.
    /// </summary>
    [Required]
    [StringLength(2048)]
    public string ContentBrowserState { get; set; } = string.Empty;
}
