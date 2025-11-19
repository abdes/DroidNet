// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.EntityFrameworkCore.Migrations;

#nullable disable

namespace Oxygen.Editor.Data.Migrations;

/// <inheritdoc />
public partial class InitialCreate : Migration
{
    private static readonly string[] Columns = ["Location", "Name"];
    private static readonly string[] SettingsColumns = ["SettingsModule", "Name", "Scope", "ScopeId"];
    private static readonly string[] WindowPlacementColumns = ["PlacementKey"];

    /// <inheritdoc />
    protected override void Up(MigrationBuilder migrationBuilder)
    {
        CreateProjectsUsageTable(migrationBuilder);
        CreateWindowPlacementsTable(migrationBuilder);
        CreateTemplatesUsageRecordsTable(migrationBuilder);
        CreateSettingsTable(migrationBuilder);
        CreateIndexes(migrationBuilder);
    }

    /// <inheritdoc />
    protected override void Down(MigrationBuilder migrationBuilder)
    {
        _ = migrationBuilder.DropTable(
            name: "WindowPlacements");
        _ = migrationBuilder.DropTable(
            name: "ProjectsUsage");
        _ = migrationBuilder.DropTable(
            name: "TemplatesUsageRecords");
        _ = migrationBuilder.DropTable(
            name: "Settings");
    }

    private static void CreateWindowPlacementsTable(MigrationBuilder migrationBuilder)
        => _ = migrationBuilder.CreateTable(
            name: "WindowPlacements",
            columns: table => new
            {
                Id = table.Column<int>(type: "INTEGER", nullable: false)
                    .Annotation(name: "Sqlite:Autoincrement", value: true),
                PlacementKey = table.Column<string>(type: "TEXT", maxLength: 255, nullable: false),
                PlacementData = table.Column<string>(type: "TEXT", maxLength: 255, nullable: false),
            },
            constraints: table => table.PrimaryKey(name: "PK_WindowPlacements", columns: x => x.Id));

    private static void CreateProjectsUsageTable(MigrationBuilder migrationBuilder)
        => _ = migrationBuilder.CreateTable(
            name: "ProjectsUsage",
            columns: table => new
            {
                Id = table.Column<int>(type: "INTEGER", nullable: false)
                    .Annotation(name: "Sqlite:Autoincrement", value: true),
                Name = table.Column<string>(type: "TEXT", maxLength: 255, nullable: false),
                Location = table.Column<string>(type: "TEXT", maxLength: 2048, nullable: false),
                TimesOpened = table.Column<int>(type: "INTEGER", nullable: false),
                LastUsedOn = table.Column<DateTime>(type: "TEXT", nullable: false),
                LastOpenedScene = table.Column<string>(type: "TEXT", maxLength: 255, nullable: false),
                ContentBrowserState = table.Column<string>(type: "TEXT", maxLength: 2048, nullable: false),
            },
            constraints: table => table.PrimaryKey(name: "PK_ProjectsUsage", columns: x => x.Id));

    private static void CreateTemplatesUsageRecordsTable(MigrationBuilder migrationBuilder)
        => _ = migrationBuilder.CreateTable(
            name: "TemplatesUsageRecords",
            columns: table => new
            {
                Id = table.Column<int>(type: "INTEGER", nullable: false)
                    .Annotation(name: "Sqlite:Autoincrement", value: true),
                Location = table.Column<string>(type: "TEXT", maxLength: 1024, nullable: false),
                LastUsedOn = table.Column<DateTime>(type: "TEXT", nullable: false),
                TimesUsed = table.Column<int>(type: "INTEGER", nullable: false),
            },
            constraints: table => table.PrimaryKey(name: "PK_TemplatesUsageRecords", columns: x => x.Id));

    private static void CreateSettingsTable(MigrationBuilder migrationBuilder)
        => _ = migrationBuilder.CreateTable(
            name: "Settings",
            columns: table => new
            {
                Id = table.Column<int>(type: "INTEGER", nullable: false)
                    .Annotation(name: "Sqlite:Autoincrement", value: true),
                SettingsModule = table.Column<string>(type: "TEXT", maxLength: 255, nullable: false),
                Name = table.Column<string>(type: "TEXT", maxLength: 255, nullable: false),
                Scope = table.Column<int>(type: "INTEGER", nullable: false),
                ScopeId = table.Column<string>(type: "TEXT", nullable: true),
                JsonValue = table.Column<string>(type: "TEXT", maxLength: 2048, nullable: true),
                CreatedAt = table.Column<DateTime>(type: "TEXT", nullable: false),
                UpdatedAt = table.Column<DateTime>(type: "TEXT", nullable: false),
            },
            constraints: table => table.PrimaryKey(name: "PK_Settings", columns: x => x.Id));

    private static void CreateIndexes(MigrationBuilder migrationBuilder)
    {
        _ = migrationBuilder.CreateIndex(
            name: "IX_ProjectsUsage_Location_Name",
            table: "ProjectsUsage",
            columns: Columns,
            unique: true);

        _ = migrationBuilder.CreateIndex(
            name: "IX_WindowPlacements_PlacementKey",
            table: "WindowPlacements",
            columns: WindowPlacementColumns,
            unique: true);

        _ = migrationBuilder.CreateIndex(
            name: "IX_TemplatesUsageRecords_Location",
            table: "TemplatesUsageRecords",
            column: "Location",
            unique: true);

        _ = migrationBuilder.CreateIndex(
            name: "IX_Settings_SettingsModule_Name_Scope_ScopeId",
            table: "Settings",
            columns: SettingsColumns,
            unique: true);
    }
}
