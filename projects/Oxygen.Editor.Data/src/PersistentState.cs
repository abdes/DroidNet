// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.EntityFrameworkCore;

/* Unmerged change from project 'Oxygen.Editor.Data (net8.0-windows10.0.22621.0)'
Added:
using Oxygen;
using Oxygen.Editor;
using Oxygen.Editor.Data;
using Oxygen.Editor.Data;
using Oxygen.Editor.Data.Models;
*/
using Oxygen.Editor.Data.Models;

namespace Oxygen.Editor.Data;

#nullable disable // These DbSet properties will be set by EF Core

/* Add example usage of the PersistentState class here */

/// <summary>
/// Represents the database context for the application.
/// </summary>
/// <param name="options">The options to be used by the DbContext.</param>
[SuppressMessage("ReSharper", "UnusedAutoPropertyAccessor.Global", Justification = "properties are set by EF Core")]
public class PersistentState(DbContextOptions<PersistentState> options) : DbContext(options)
{
    /// <summary>
    /// Gets or sets the DbSet for template usage records.
    /// </summary>
    public DbSet<TemplateUsage> TemplatesUsageRecords { get; set; }

    /// <summary>
    /// Gets or sets the DbSet for project usage records.
    /// </summary>
    public DbSet<ProjectUsage> ProjectUsageRecords { get; set; }

    /// <summary>
    /// Gets or sets the DbSet for module settings.
    /// </summary>
    public DbSet<ModuleSetting> Settings { get; set; }
}
