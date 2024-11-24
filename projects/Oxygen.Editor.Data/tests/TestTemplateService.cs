// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Oxygen.Editor.Data.Models;

namespace Oxygen.Editor.Data.Tests;

[ExcludeFromCodeCoverage]
[SuppressMessage("Microsoft.Performance", "CA1812", Justification = "Instantiated by dependency injection container.")]
internal sealed class TestTemplateService(PersistentState context)
{
    public IEnumerable<TemplateUsage> GetAllTemplates() => [.. context.TemplatesUsageRecords];

    public void AddTemplate(TemplateUsage template)
    {
        _ = context.TemplatesUsageRecords.Add(template);
        _ = context.SaveChanges();
    }
}
