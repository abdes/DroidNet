// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Data;

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.VisualStudio.TestTools.UnitTesting;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(DatabaseSchemaTests))]
public class DatabaseSchemaTests
{
    private readonly IServiceProvider serviceProvider;

    public DatabaseSchemaTests()
    {
        var builder = Host.CreateDefaultBuilder();
        _ = builder.ConfigureServices(sc => sc.AddSqlite<PersistentState>("Data Source=memory"));
        var host = builder.Build();

        this.serviceProvider = host.Services;

        using var scope = this.serviceProvider.CreateScope();
        var db = scope.ServiceProvider.GetRequiredService<PersistentState>();
        db.Database.Migrate();
    }

    [TestMethod]
    public void HasRecentlyUsedTemplatesCollection()
    {
        using var scope = this.serviceProvider.CreateScope();
        var state = scope.ServiceProvider.GetRequiredService<PersistentState>();

        _ = state.RecentlyUsedTemplates.Should().NotBeNull();
    }

    [TestMethod]
    public void HasRecentlyUsedProjectsCollection()
    {
        using var scope = this.serviceProvider.CreateScope();
        var state = scope.ServiceProvider.GetRequiredService<PersistentState>();

        _ = state.RecentlyUsedProjects.Should().NotBeNull();
    }
}
