// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.EntityFrameworkCore;
using Microsoft.EntityFrameworkCore.Metadata.Builders;

namespace Oxygen.Editor.Data.Models;

/// <summary>
/// Provides configuration for the <see cref="ProjectUsage"/> entity.
/// </summary>
/// <remarks>
/// Configures a unique index on the Location and Name properties, ensuring that
/// the combination of Location and Name can be used to obtain Project Usage data.
/// </remarks>
public class ProjectUsageConfiguration : IEntityTypeConfiguration<ProjectUsage>
{
    /// <inheritdoc/>
    public void Configure(EntityTypeBuilder<ProjectUsage> builder) =>
        _ = builder.HasIndex(p => new { p.Location, p.Name }).IsUnique();
}
