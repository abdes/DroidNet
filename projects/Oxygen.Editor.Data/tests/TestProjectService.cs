// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.TestHelpers;
using Oxygen.Editor.Data.Models;
using Serilog.Events;

namespace Oxygen.Editor.Data.Tests;

[ExcludeFromCodeCoverage]
[SuppressMessage("Microsoft.Performance", "CA1812", Justification = "Instantiated by dependency injection container.")]
internal sealed class TestProjectService(PersistentState context)
{
    public IEnumerable<ProjectUsage> GetAllProjects() => [.. context.ProjectUsageRecords];

    public void AddProject(ProjectUsage project)
    {
        CommonTestEnv.LoggingLevelSwitch.MinimumLevel = LogEventLevel.Debug;
        _ = context.ProjectUsageRecords.Add(project);
        _ = context.SaveChanges();
        CommonTestEnv.LoggingLevelSwitch.MinimumLevel = LogEventLevel.Warning;
    }
}
