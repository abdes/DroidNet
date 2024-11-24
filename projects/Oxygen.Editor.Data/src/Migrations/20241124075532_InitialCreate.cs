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

    /// <inheritdoc />
    protected override void Up(MigrationBuilder migrationBuilder)
    {
        _ = migrationBuilder.CreateTable(
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

        _ = migrationBuilder.CreateTable(
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

        _ = migrationBuilder.CreateIndex(
            name: "IX_ProjectsUsage_Location_Name",
            table: "ProjectsUsage",
            columns: Columns,
            unique: true);

        _ = migrationBuilder.CreateIndex(
            name: "IX_TemplatesUsageRecords_Location",
            table: "TemplatesUsageRecords",
            column: "Location",
            unique: true);
    }

    /// <inheritdoc />
    protected override void Down(MigrationBuilder migrationBuilder)
    {
        _ = migrationBuilder.DropTable(
            name: "ProjectsUsage");

        _ = migrationBuilder.DropTable(
            name: "TemplatesUsageRecords");
    }
}
