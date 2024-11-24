// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.EntityFrameworkCore;
using Microsoft.EntityFrameworkCore.Metadata.Builders;

namespace Oxygen.Editor.Data.Models;

/// <summary>
/// Provides configuration for the <see cref="TemplateUsage"/> entity.
/// </summary>
public class TemplateUsageConfiguration : IEntityTypeConfiguration<TemplateUsage>
{
    /// <inheritdoc/>
    public void Configure(EntityTypeBuilder<TemplateUsage> builder) =>
        _ = builder.HasIndex(t => t.Location).IsUnique();
}
