# Oxygen Editor Data Layer

## Initial Database Creation

To create the initial database schema, you can use the `dotnet ef migrations add` command. The `DesignTimePersistentStateFactory` class supports the following command line arguments:

- `--mode`: Specifies the configuration mode for the `PathFinder` (dev or real), used to obtain the path for the persistent store database. Default is dev.
- `--use-in-memory-db`: Do not use a database file and use an in-memory SQLite database. Particularly useful for testing and initial migration creation.

**Note:** Only one of the options should be used at a time. If both are specified, an `ArgumentException` is thrown.

### Example Usage

Using an in-memory SQLite database:

```shell
dotnet ef migrations add InitialCreate --framework net8.0 --msbuildprojectextensionspath ..\..\..\obj\projects\Oxygen.Editor.Data\src\ -- --use-in-memory-db
```

Using a file-based SQLite database in development mode:

```shell
dotnet ef migrations add InitialCreate --framework net8.0 --msbuildprojectextensionspath ..\..\..\obj\projects\Oxygen.Editor.Data\src\ -- --mode=dev
```

## Migrations

### Adding a Migration

When you make changes to your model, add a new migration to capture those changes.

```shell
dotnet ef migrations add <MIGRATION_NAME> -Project Data
```

### Reviewing the Migration

Review the generated migration code to ensure it accurately reflects the intended changes.

```shell
public partial class <MIGRATION_NAME> : Migration
{
    protected override void Up(MigrationBuilder migrationBuilder)
    {
        // Migration code here
    }

    protected override void Down(MigrationBuilder migrationBuilder)
    {
        // Revert migration code here
    }
}
```

### Applying the Migration

Apply the migration to update the database schema.

```shell
dotnet ef database update -Project Data
```

### Updating the Model

Ensure your model classes reflect the changes.

```shell
public class ProjectState
{
    [Key]
    public int Id { get; set; }

    [Required]
    public string Location { get; set; } = string.Empty;

    [Required]
    public string Name { get; set; } = string.Empty;

    [Required]
    public DateTime LastUsedOn { get; set; } = DateTime.Now;

    [Required]
    public string LastOpenedScene { get; set; } = string.Empty;

    [Required]
    public string ContentBrowserState { get; set; } = string.Empty;

    public string? NewField { get; set; } // New field added
}
```

### Handling Existing Data

When making changes to the model, consider how existing data will be affected:

- **Adding Columns**: Adding new columns is generally safe. You can provide default values or allow nulls to avoid issues with existing rows.
- **Renaming Columns**: Renaming columns requires careful handling. EF Core does not support column renaming directly, so you may need to use raw SQL or custom migration code.
- **Removing Columns**: Removing columns can lead to data loss. Ensure that any necessary data is backed up or migrated to new columns before dropping the old columns.

### Example of a Complex Migration

If you need to rename a column, you might write a custom migration:

```shell
public partial class RenameContentBrowserState : Migration
{
    protected override void Up(MigrationBuilder migrationBuilder)
    {
        migrationBuilder.RenameColumn(
            name: "ContentBrowserState",
            table: "ProjectStates",
            newName: "BrowserState");
    }

    protected override void Down(MigrationBuilder migrationBuilder)
    {
        migrationBuilder.RenameColumn(
            name: "BrowserState",
            table: "ProjectStates",
            newName: "ContentBrowserState");
    }
}
```

### Summary

- **Migrations**: Use EF Core migrations to manage schema changes.
- **Version Control**: Keep migrations in version control.
- **Backward Compatibility**: Plan changes to avoid breaking existing functionality.
- **Data Seeding**: Use data seeding to manage initial and updated data.
