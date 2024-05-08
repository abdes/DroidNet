// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

// <auto-generated>
// Entity framework migration, then slightly modified and formatted.
// </auto-generated>

namespace Oxygen.Editor.Data.Migrations;

using Microsoft.EntityFrameworkCore.Migrations;

/// <inheritdoc />
public partial class ProjectBrowserState : Migration
{
    /// <inheritdoc />
    protected override void Up(MigrationBuilder migrationBuilder)
    {
        _ = migrationBuilder.AddColumn<int>(
            name: "ProjectBrowserStateId",
            table: "RecentlyUsedTemplates",
            type: "INTEGER",
            nullable: false,
            defaultValue: 1);

        _ = migrationBuilder.AddColumn<int>(
            name: "ProjectBrowserStateId",
            table: "RecentlyUsedProjects",
            type: "INTEGER",
            nullable: false,
            defaultValue: 1);

        _ = migrationBuilder.CreateTable(
            name: "ProjectBrowserStates",
            columns: table => new
            {
                Id = table.Column<int>(type: "INTEGER", nullable: false)
                    .Annotation("Sqlite:Autoincrement", value: true),
                LastSaveLocation = table.Column<string>(type: "TEXT", nullable: false),
            },
            constraints: table =>
            {
                _ = table.PrimaryKey("PK_ProjectBrowserStates", x => x.Id);
                _ = table.CheckConstraint("CK_Single_Row", "[Id] = 1");
            });

        _ = migrationBuilder.InsertData(
            table: "ProjectBrowserStates",
            columns: ["Id", "LastSaveLocation"],
            values: [1, string.Empty]);

        _ = migrationBuilder.CreateIndex(
            name: "IX_RecentlyUsedTemplates_Location",
            table: "RecentlyUsedTemplates",
            column: "Location");

        _ = migrationBuilder.CreateIndex(
            name: "IX_RecentlyUsedTemplates_ProjectBrowserStateId",
            table: "RecentlyUsedTemplates",
            column: "ProjectBrowserStateId");

        _ = migrationBuilder.CreateIndex(
            name: "IX_RecentlyUsedProjects_Location",
            table: "RecentlyUsedProjects",
            column: "Location");

        _ = migrationBuilder.CreateIndex(
            name: "IX_RecentlyUsedProjects_ProjectBrowserStateId",
            table: "RecentlyUsedProjects",
            column: "ProjectBrowserStateId");

        _ = migrationBuilder.AddForeignKey(
            name: "FK_RecentlyUsedProjects_ProjectBrowserStates_ProjectBrowserStateId",
            table: "RecentlyUsedProjects",
            column: "ProjectBrowserStateId",
            principalTable: "ProjectBrowserStates",
            principalColumn: "Id",
            onDelete: ReferentialAction.Cascade);

        _ = migrationBuilder.AddForeignKey(
            name: "FK_RecentlyUsedTemplates_ProjectBrowserStates_ProjectBrowserStateId",
            table: "RecentlyUsedTemplates",
            column: "ProjectBrowserStateId",
            principalTable: "ProjectBrowserStates",
            principalColumn: "Id",
            onDelete: ReferentialAction.Cascade);
    }

    /// <inheritdoc />
    protected override void Down(MigrationBuilder migrationBuilder)
    {
        _ = migrationBuilder.DropForeignKey(
            name: "FK_RecentlyUsedProjects_ProjectBrowserStates_ProjectBrowserStateId",
            table: "RecentlyUsedProjects");

        _ = migrationBuilder.DropForeignKey(
            name: "FK_RecentlyUsedTemplates_ProjectBrowserStates_ProjectBrowserStateId",
            table: "RecentlyUsedTemplates");

        _ = migrationBuilder.DropTable(
            name: "ProjectBrowserStates");

        _ = migrationBuilder.DropIndex(
            name: "IX_RecentlyUsedTemplates_Location",
            table: "RecentlyUsedTemplates");

        _ = migrationBuilder.DropIndex(
            name: "IX_RecentlyUsedTemplates_ProjectBrowserStateId",
            table: "RecentlyUsedTemplates");

        _ = migrationBuilder.DropIndex(
            name: "IX_RecentlyUsedProjects_Location",
            table: "RecentlyUsedProjects");

        _ = migrationBuilder.DropIndex(
            name: "IX_RecentlyUsedProjects_ProjectBrowserStateId",
            table: "RecentlyUsedProjects");

        _ = migrationBuilder.DropColumn(
            name: "ProjectBrowserStateId",
            table: "RecentlyUsedTemplates");

        _ = migrationBuilder.DropColumn(
            name: "ProjectBrowserStateId",
            table: "RecentlyUsedProjects");
    }
}
