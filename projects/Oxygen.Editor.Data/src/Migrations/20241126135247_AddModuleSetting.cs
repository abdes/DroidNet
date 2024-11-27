// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.EntityFrameworkCore.Migrations;

#nullable disable

namespace Oxygen.Editor.Data.Migrations;

/// <inheritdoc />
public partial class AddModuleSetting : Migration
{
    private static readonly string[] Columns = ["ModuleName", "Key"];

    /// <inheritdoc />
    protected override void Up(MigrationBuilder migrationBuilder)
    {
        _ = migrationBuilder.CreateTable(
            name: "Settings",
            columns: table => new
            {
                Id = table.Column<int>(type: "INTEGER", nullable: false)
                    .Annotation(name: "Sqlite:Autoincrement", value: true),
                ModuleName = table.Column<string>(type: "TEXT", maxLength: 255, nullable: false),
                Key = table.Column<string>(type: "TEXT", maxLength: 255, nullable: false),
                JsonValue = table.Column<string>(type: "TEXT", maxLength: 2048, nullable: true),
                CreatedAt = table.Column<DateTime>(type: "TEXT", nullable: false),
                UpdatedAt = table.Column<DateTime>(type: "TEXT", nullable: false),
            },
            constraints: table => table.PrimaryKey(name: "PK_Settings", columns: x => x.Id));

        _ = migrationBuilder.CreateIndex(
            name: "IX_Settings_ModuleName_Key",
            table: "Settings",
            columns: Columns,
            unique: true);
    }

    /// <inheritdoc />
    protected override void Down(MigrationBuilder migrationBuilder) => migrationBuilder.DropTable(
            name: "Settings");
}
