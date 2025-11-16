// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DryIoc;
using Microsoft.EntityFrameworkCore;
using Oxygen.Editor.Data.Models;

namespace Oxygen.Editor.Data.Tests;

/// <summary>
/// Contains unit tests for verifying the database schema.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Database Schema")]
public partial class DatabaseSchemaTests : DatabaseTests
{
    public DatabaseSchemaTests()
    {
        this.Container.Register<TestProjectService>(Reuse.Scoped);
        this.Container.Register<TestTemplateService>(Reuse.Scoped);
    }

    /// <summary>
    /// Verifies that the database has a table called "ProjectsUsage".
    /// </summary>
    [TestMethod]
    public void DatabaseHasProjectsTable()
    {
        using var scope = this.Container.OpenScope();
        var db = scope.Resolve<PersistentState>();

        using var command = db.Database.GetDbConnection().CreateCommand();
        command.CommandText = $"SELECT name FROM sqlite_master WHERE type='table' AND name='{ProjectUsage.TableName}';";
        var tableName = command.ExecuteScalar() as string;

        _ = tableName.Should().Be(ProjectUsage.TableName);
    }

    /// <summary>
    /// Verifies that the database has a table called "TemplatesUsageRecords".
    /// </summary>
    [TestMethod]
    public void DatabaseHasTemplatesTable()
    {
        using var scope = this.Container.OpenScope();
        var db = scope.Resolve<PersistentState>();

        using var command = db.Database.GetDbConnection().CreateCommand();
        command.CommandText = $"SELECT name FROM sqlite_master WHERE type='table' AND name='{TemplateUsage.TableName}';";
        var tableName = command.ExecuteScalar() as string;

        _ = tableName.Should().Be(TemplateUsage.TableName);
    }

    /// <summary>
    /// Verifies that the TestProjectService can be resolved and used.
    /// </summary>
    [TestMethod]
    public void CanResolveProjectService()
    {
        using var scope = this.Container.OpenScope();
        var projectService = scope.Resolve<TestProjectService>();

        _ = projectService.Should().NotBeNull();
    }

    /// <summary>
    /// Verifies that the TestProjectService can add and retrieve projects.
    /// </summary>
    [TestMethod]
    public void ProjectServiceCanAddAndRetrieveProjects()
    {
        using var scope = this.Container.OpenScope();
        var projectService = scope.Resolve<TestProjectService>();

        var project = new ProjectUsage
        {
            Location = "Test Location",
            Name = "Test Project",
            LastUsedOn = DateTime.UtcNow,
            LastOpenedScene = "Test Scene",
            ContentBrowserState = "Test State",
            TimesOpened = 1,
        };

        projectService.AddProject(project);

        var projects = projectService.GetAllProjects();
        _ = projects.Should().ContainSingle(p => p.Name == "Test Project");
    }

    /// <summary>
    /// Verifies that the TestTemplateService can add and retrieve templates.
    /// </summary>
    [TestMethod]
    public void TemplateServiceCanAddAndRetrieveTemplates()
    {
        using var scope = this.Container.OpenScope();
        var templateService = scope.Resolve<TestTemplateService>();

        var template = new TemplateUsage
        {
            Location = "Test Location",
            LastUsedOn = DateTime.UtcNow,
            TimesUsed = 1,
        };

        templateService.AddTemplate(template);

        var templates = templateService.GetAllTemplates();
        _ = templates.Should().ContainSingle(t => t.Location == "Test Location");
    }

    /// <summary>
    /// Verifies that the database has a table called "ModuleSettings".
    /// </summary>
    [TestMethod]
    public void DatabaseHasModuleSettingsTable()
    {
        using var scope = this.Container.OpenScope();
        var db = scope.Resolve<PersistentState>();

        using var command = db.Database.GetDbConnection().CreateCommand();
        command.CommandText = $"SELECT name FROM sqlite_master WHERE type='table' AND name='{ModuleSetting.TableName}';";
        var tableName = command.ExecuteScalar() as string;

        _ = tableName.Should().Be(ModuleSetting.TableName);
    }

    /// <summary>
    /// Verifies that the indexes defined in the initial migration exist.
    /// </summary>
    [TestMethod]
    public void DatabaseHasIndexes()
    {
        using var scope = this.Container.OpenScope();
        var db = scope.Resolve<PersistentState>();

        using var command = db.Database.GetDbConnection().CreateCommand();

        command.CommandText = "SELECT name FROM sqlite_master WHERE type='index' AND name='IX_ProjectsUsage_Location_Name';";
        var projectsIndex = command.ExecuteScalar() as string;
        _ = projectsIndex.Should().Be("IX_ProjectsUsage_Location_Name");

        command.CommandText = "SELECT name FROM sqlite_master WHERE type='index' AND name='IX_TemplatesUsageRecords_Location';";
        var templatesIndex = command.ExecuteScalar() as string;
        _ = templatesIndex.Should().Be("IX_TemplatesUsageRecords_Location");

        command.CommandText = "SELECT name FROM sqlite_master WHERE type='index' AND name='IX_Settings_SettingsModule_Name_Scope_ScopeId';";
        var settingsIndex = command.ExecuteScalar() as string;
        _ = settingsIndex.Should().Be("IX_Settings_SettingsModule_Name_Scope_ScopeId");
    }

    /// <summary>
    /// Verifies that a ModuleSetting can be added and retrieved.
    /// </summary>
    [TestMethod]
    public void CanAddAndRetrieveModuleSetting()
    {
        using var scope = this.Container.OpenScope();
        var db = scope.Resolve<PersistentState>();

        var moduleSetting = new ModuleSetting
        {
            SettingsModule = "TestModule",
            Name = "TestKey",
            JsonValue = "{\"setting\":\"value\"}",
            CreatedAt = DateTime.UtcNow,
            UpdatedAt = DateTime.UtcNow,
        };

        _ = db.Settings.Add(moduleSetting);
        _ = db.SaveChanges();

        var retrievedSetting = db.Settings.Single(ms => ms.SettingsModule == "TestModule" && ms.Name == "TestKey");
        _ = retrievedSetting.Should().NotBeNull();
        _ = retrievedSetting.JsonValue.Should().Be("{\"setting\":\"value\"}");
    }
}
