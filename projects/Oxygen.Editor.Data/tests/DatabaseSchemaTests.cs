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
///     Contains unit tests for verifying the database schema.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Database Schema")]
public class DatabaseSchemaTests : DatabaseTests
{
    private IResolverContext scope;
    private PersistentState db;

    /// <summary>
    ///     Initializes a new instance of the <see cref="DatabaseSchemaTests"/> class. Registers
    ///     test services and resolves the in-memory database scope used by the tests.
    /// </summary>
    public DatabaseSchemaTests()
    {
        this.Container.Register<TestProjectService>(Reuse.Scoped);
        this.Container.Register<TestTemplateService>(Reuse.Scoped);

        this.scope = this.Container.OpenScope();
        this.db = this.scope.Resolve<PersistentState>();
    }

    /// <summary>
    ///     Verifies that the database migration created the table for <see cref="ProjectUsage"/>
    ///     (Table name: "ProjectsUsage").
    /// </summary>
    [TestMethod]
    public void Migration_CreatesTable_ProjectsUsage()
    {
        using var command = this.db.Database.GetDbConnection().CreateCommand();
        command.CommandText = "SELECT name FROM sqlite_master WHERE type='table' AND name = @name;";
        var param = command.CreateParameter();
        param.ParameterName = "@name";
        param.Value = ProjectUsage.TableName;
        _ = command.Parameters.Add(param);
        var tableName = command.ExecuteScalar() as string;

        _ = tableName.Should().Be(ProjectUsage.TableName);
    }

    /// <summary>
    ///     Verifies that the database migration created the table for <see cref="TemplateUsage"/>
    ///     (Table name: "TemplatesUsageRecords").
    /// </summary>
    [TestMethod]
    public void Migration_CreatesTable_TemplatesUsageRecords()
    {
        using var command = this.db.Database.GetDbConnection().CreateCommand();
        command.CommandText = "SELECT name FROM sqlite_master WHERE type='table' AND name = @name;";
        var param = command.CreateParameter();
        param.ParameterName = "@name";
        param.Value = TemplateUsage.TableName;
        _ = command.Parameters.Add(param);
        var tableName = command.ExecuteScalar() as string;

        _ = tableName.Should().Be(TemplateUsage.TableName);
    }

    /// <summary>
    ///     Verifies that the <see cref="TestProjectService"/> can be resolved from the container
    ///     and is not null.
    /// </summary>
    [TestMethod]
    public void TestSetup_Container_CanResolve_TestProjectService()
    {
        var projectService = this.scope.Resolve<TestProjectService>();

        _ = projectService.Should().NotBeNull();
    }

    /// <summary>
    ///     Verifies that a <see cref="ModuleSetting"/> can be added to the persistent state and
    ///     retrieved.
    /// </summary>
    [TestMethod]
    public void Entities_ModuleSetting_CanAddRetrieve()
    {
        var moduleSetting = new ModuleSetting
        {
            SettingsModule = "TestModule",
            Name = "TestKey",
            JsonValue = "{\"setting\":\"value\"}",
            CreatedAt = DateTime.UtcNow,
            UpdatedAt = DateTime.UtcNow,
        };

        _ = this.db.Settings.Add(moduleSetting);
        _ = this.db.SaveChanges();

        var retrievedSetting = this.db.Settings.Single(ms => ms.SettingsModule == "TestModule" && ms.Name == "TestKey");
        _ = retrievedSetting.Should().NotBeNull();
        _ = retrievedSetting.JsonValue.Should().Be("{\"setting\":\"value\"}");
    }

    /// <summary>
    ///     Verifies that the <see cref="TestProjectService"/> can add and retrieve projects.
    /// </summary>
    [TestMethod]
    public void Entities_ProjectUsage_CanAddRetrieve()
    {
        var projectService = this.scope.Resolve<TestProjectService>();

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
    ///     Verifies that the <see cref="TestTemplateService"/> can add and retrieve templates.
    /// </summary>
    [TestMethod]
    public void Entities_TemplateUsage_CanAddRetrieve()
    {
        var templateService = this.scope.Resolve<TestTemplateService>();

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
    ///     Verifies that the database migration created the table for <see cref="ModuleSetting"/>
    ///     (Table name: "ModuleSettings").
    /// </summary>
    [TestMethod]
    public void Migration_CreatesTable_Settings()
    {
        using var command = this.db.Database.GetDbConnection().CreateCommand();
        command.CommandText = "SELECT name FROM sqlite_master WHERE type='table' AND name = @name;";
        var param = command.CreateParameter();
        param.ParameterName = "@name";
        param.Value = ModuleSetting.TableName;
        _ = command.Parameters.Add(param);
        var tableName = command.ExecuteScalar() as string;

        _ = tableName.Should().Be(ModuleSetting.TableName);
    }

    /// <summary>
    ///     Verifies the index IX_ProjectsUsage_Location_Name exists in the database.
    /// </summary>
    [TestMethod]
    public void Database_Index_Exists_IX_ProjectsUsage_Location_Name()
    {
        using var command = this.db.Database.GetDbConnection().CreateCommand();

        command.CommandText = "SELECT name FROM sqlite_master WHERE type='index' AND name = @name;";
        var param = command.CreateParameter();
        param.ParameterName = "@name";
        param.Value = "IX_ProjectsUsage_Location_Name";
        _ = command.Parameters.Add(param);
        var projectsIndex = command.ExecuteScalar() as string;
        _ = projectsIndex.Should().Be("IX_ProjectsUsage_Location_Name");
    }

    /// <summary>
    ///     Verifies the index IX_TemplatesUsageRecords_Location exists in the database.
    /// </summary>
    [TestMethod]
    public void Database_Index_Exists_IX_TemplatesUsageRecords_Location()
    {
        using var command = this.db.Database.GetDbConnection().CreateCommand();
        command.CommandText = "SELECT name FROM sqlite_master WHERE type='index' AND name = @name;";
        var param = command.CreateParameter();
        param.ParameterName = "@name";
        param.Value = "IX_TemplatesUsageRecords_Location";
        _ = command.Parameters.Add(param);
        var templatesIndex = command.ExecuteScalar() as string;
        _ = templatesIndex.Should().Be("IX_TemplatesUsageRecords_Location");
    }

    /// <summary>
    ///     Verifies the index IX_Settings_SettingsModule_Name_Scope_ScopeId exists in the database.
    /// </summary>
    [TestMethod]
    public void Database_Index_Exists_IX_Settings_SettingsModule_Name_Scope_ScopeId()
    {
        using var command = this.db.Database.GetDbConnection().CreateCommand();
        command.CommandText = "SELECT name FROM sqlite_master WHERE type='index' AND name = @name;";
        var param = command.CreateParameter();
        param.ParameterName = "@name";
        param.Value = "IX_Settings_SettingsModule_Name_Scope_ScopeId";
        _ = command.Parameters.Add(param);
        var settingsIndex = command.ExecuteScalar() as string;
        _ = settingsIndex.Should().Be("IX_Settings_SettingsModule_Name_Scope_ScopeId");
    }

    /// <summary>
    ///     Verifies that the <see cref="ModuleSetting"/> table has a primary key and that it
    ///     includes the Id property.
    /// </summary>
    [TestMethod]
    public void Entities_ModuleSetting_HasPrimaryKey()
        => _ = TableHasPrimaryKey(this.db, ModuleSetting.TableName).Should().BeTrue();

    /// <summary>
    ///     Verifies that the <see cref="ProjectUsage"/> table has a primary key and that it
    ///     includes the Id property.
    /// </summary>
    [TestMethod]
    public void Entities_ProjectUsage_HasPrimaryKey()
        => _ = TableHasPrimaryKey(this.db, ProjectUsage.TableName).Should().BeTrue();

    /// <summary>
    ///     Verifies that the <see cref="TemplateUsage"/> table has a primary key and that it
    ///     includes the Id property.
    /// </summary>
    [TestMethod]
    public void Entities_TemplateUsage_HasPrimaryKey()
        => _ = TableHasPrimaryKey(this.db, TemplateUsage.TableName).Should().BeTrue();

    /// <summary>
    ///     Verifies that required columns for <see cref="TemplateUsage"/> are not nullable.
    /// </summary>
    [TestMethod]
    public void Entities_TemplateUsage_RequiredColumnsAreNotNull()
        => _ = ColumnIsNotNull(this.db, TemplateUsage.TableName, "Location").Should().BeTrue();

    /// <summary>
    ///     Verifies that required columns for <see cref="ProjectUsage"/> are not nullable.
    /// </summary>
    [TestMethod]
    public void Entities_ProjectUsage_RequiredColumnsAreNotNull()
    {
        _ = ColumnIsNotNull(this.db, ProjectUsage.TableName, "Name").Should().BeTrue();
        _ = ColumnIsNotNull(this.db, ProjectUsage.TableName, "Location").Should().BeTrue();
    }

    /// <summary>
    ///     Verifies that required columns for <see cref="ModuleSetting"/> are not nullable.
    /// </summary>
    [TestMethod]
    public void Entities_ModuleSetting_RequiredColumnsAreNotNull()
    {
        _ = ColumnIsNotNull(this.db, ModuleSetting.TableName, "SettingsModule").Should().BeTrue();
        _ = ColumnIsNotNull(this.db, ModuleSetting.TableName, "Name").Should().BeTrue();
        _ = ColumnIsNotNull(this.db, ModuleSetting.TableName, "Scope").Should().BeTrue();
    }

    /// <summary>
    ///     Disposes resources used by the test class, including test-level DI scope and database
    ///     instance.
    /// </summary>
    /// <param name="disposing">True when called from Dispose; false when called from
    /// finalizer.</param>
    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            this.scope?.Dispose();
            this.scope = null!;
            this.db?.Dispose();
            this.db = null!;
        }

        base.Dispose(disposing);
    }

    /// <summary>
    ///     Returns true if the given table has a primary key that includes an Id property.
    /// </summary>
    /// <param name="db">The <see cref="PersistentState"/> to inspect.</param>
    /// <param name="tableName">The name of the table to check.</param>
    private static bool TableHasPrimaryKey(PersistentState db, string tableName)
    {
        var entityType = db.Model.GetEntityTypes().FirstOrDefault(et => string.Equals(et.GetTableName(), tableName, StringComparison.OrdinalIgnoreCase));
        if (entityType == null)
        {
            return false;
        }

        var pk = entityType.FindPrimaryKey();
        return pk?.Properties.Any(p => p.Name.Equals("Id", StringComparison.OrdinalIgnoreCase)) == true;
    }

    /// <summary>
    ///     Returns true if the specified column in the given table is not nullable.
    /// </summary>
    /// <param name="db">The <see cref="PersistentState"/> to inspect.</param>
    /// <param name="tableName">The name of the table that contains the column.</param>
    /// <param name="columnName">The name of the column to inspect.</param>
    private static bool ColumnIsNotNull(PersistentState db, string tableName, string columnName)
    {
        var entityType = db.Model.GetEntityTypes().FirstOrDefault(et => string.Equals(et.GetTableName(), tableName, StringComparison.OrdinalIgnoreCase));
        if (entityType == null)
        {
            return false;
        }

        var property = entityType.FindProperty(columnName)
            ?? entityType.GetProperties().FirstOrDefault(p => string.Equals(p.GetColumnName(), columnName, StringComparison.OrdinalIgnoreCase));
        return property?.IsNullable == false;
    }
}
