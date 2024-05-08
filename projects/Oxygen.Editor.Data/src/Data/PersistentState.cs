// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Data;

using Microsoft.EntityFrameworkCore;
using Oxygen.Editor.Data.Models;

#nullable disable

public class PersistentState : DbContext
{
    public PersistentState(DbContextOptions<PersistentState> options)
        : base(options)
    {
    }

    public DbSet<ProjectBrowserState> ProjectBrowserStates { get; set; }

    public ProjectBrowserState ProjectBrowserState => this.ProjectBrowserStates!.FirstOrDefault()!;

    public DbSet<RecentlyUsedProject> RecentlyUsedProjects { get; set; }

    public DbSet<RecentlyUsedTemplate> RecentlyUsedTemplates { get; set; }

    protected override void OnModelCreating(ModelBuilder modelBuilder) => modelBuilder.Entity<ProjectBrowserState>()
        .ToTable(t => t.HasCheckConstraint("CK_Single_Row", "[Id] = 1"))
        .HasData(
            new
            {
                Id = 1,
                LastSaveLocation = string.Empty,
                RecentProjects = new List<RecentlyUsedProject>(),
                RecentTemplates = new List<RecentlyUsedTemplate>(),
            });
}
