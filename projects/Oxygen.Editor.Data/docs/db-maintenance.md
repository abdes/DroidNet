# Database Maintenance Guide

This guide provides comprehensive instructions for managing the Oxygen Editor's persistent store using Entity Framework Core and related tooling. It covers database migrations, schema updates, troubleshooting, and best practices for developers.

## Table of Contents

- [Prerequisites](#prerequisites)
- [Quick Start](#quick-start)
- [Understanding the Design-Time Factory](#understanding-the-design-time-factory)
- [Common Workflows](#common-workflows)
- [Migration Management](#migration-management)
- [Database Operations](#database-operations)
- [Troubleshooting](#troubleshooting)
- [Best Practices](#best-practices)
- [Advanced Scenarios](#advanced-scenarios)

## Prerequisites

### Required Software

#### 1. .NET SDK 9.0 or later

Verify installation:

```powershell
dotnet --version
```

If not installed, the repository's `init.ps1` script will install it for you.

#### 2. Entity Framework Core CLI Tools

Install globally:

```powershell
dotnet tool install --global dotnet-ef
```

Update existing installation:

```powershell
dotnet tool update --global dotnet-ef
```

Verify installation:

```powershell
dotnet ef --version
```

### Required NuGet Packages

The following packages are already referenced in `Oxygen.Editor.Data.csproj`:

- `Microsoft.EntityFrameworkCore` (9.0+)
- `Microsoft.EntityFrameworkCore.Sqlite` (9.0+)
- `Microsoft.EntityFrameworkCore.Design` (9.0+)

To verify or add packages:

```powershell
cd projects/Oxygen.Editor.Data/src
dotnet add package Microsoft.EntityFrameworkCore.Design
dotnet add package Microsoft.EntityFrameworkCore.Sqlite
```

## Quick Start

### For New Developers

If you're setting up the project for the first time:

1. **Clone the repository and run init script:**

   ```powershell
   cd f:\projects\DroidNet
   pwsh ./init.ps1
   ```

2. **Build the project:**

   ```powershell
   cd projects/Oxygen.Editor.Data/src
   dotnet build
   ```

3. **The database will be created automatically** when the application first runs. EF Core will apply all pending migrations automatically via `context.Database.Migrate()`.

### For Database Schema Changes

When you modify data model classes:

1. Create a migration (see [Creating Migrations](#creating-migrations))
2. Review the generated code
3. Apply the migration (see [Applying Migrations](#applying-migrations))
4. Commit both model changes and migration files to version control

## Understanding the Design-Time Factory

The `DesignTimePersistentStateFactory` class enables `dotnet ef` to create a `PersistentState` DbContext at design time without running the full application. This is essential for migrations and other EF Core tools.

### Factory Command-Line Arguments

The factory supports three mutually exclusive modes for specifying the database location:

| Argument | Description | Use Case |
|----------|-------------|----------|
| `--mode <dev\|real>` | Uses `PathFinder` to resolve database location. Default: `dev` | Normal development workflow |
| `--use-in-memory-db` | Creates in-memory SQLite database | Safe migration generation, testing |
| `--db-path <path>` | Explicit path to database file | CI/CD, custom database location |

**Important:** Only use ONE of these options at a time. Using multiple will throw an `ArgumentException`.

### Why Use In-Memory for Migrations?

Creating migrations with `--use-in-memory-db` is recommended because:

- No file locking issues
- Deterministic schema generation (no existing data interference)
- Faster execution
- No cleanup required

You can then apply migrations to the actual database file later.

## Common Workflows

### Creating Migrations

**Scenario:** You've added a new property to an entity or created a new entity class.

**Recommended approach (in-memory):**

```powershell
# From repository root
dotnet ef migrations add <DescriptiveName> `
  --project projects/Oxygen.Editor.Data/src `
  --startup-project projects/Oxygen.Editor.Data/src `
  --framework net9.0 `
  -- --use-in-memory-db
```

**Alternative (with development database):**

```powershell
dotnet ef migrations add <DescriptiveName> `
  --project projects/Oxygen.Editor.Data/src `
  --startup-project projects/Oxygen.Editor.Data/src `
  --framework net9.0 `
  -- --mode=dev
```

**With explicit database path:**

```powershell
dotnet ef migrations add <DescriptiveName> `
  --project projects/Oxygen.Editor.Data/src `
  --startup-project projects/Oxygen.Editor.Data/src `
  --framework net9.0 `
  -- --db-path="F:\path\to\PersistentState.db"
```

**Result:** A new migration file is created in `projects/Oxygen.Editor.Data/src/Migrations/` with a timestamp prefix.

### Reviewing Migrations

**Always review generated migration code before applying:**

1. Open `Migrations/<timestamp>_<Name>.cs`
2. Check the `Up()` method for expected schema changes
3. Check the `Down()` method for proper rollback logic
4. Verify column types, constraints, and indexes

**Common issues to check:**

- Data loss operations (column drops, table drops)
- Missing default values for non-nullable columns
- Index naming conflicts
- Incorrect data type mappings

### Applying Migrations

**Apply all pending migrations to database:**

```powershell
# Using development mode (PathFinder resolves location)
dotnet ef database update `
  --project projects/Oxygen.Editor.Data/src `
  --startup-project projects/Oxygen.Editor.Data/src `
  --framework net9.0 `
  -- --mode=dev
```

**Apply to specific database file:**

```powershell
dotnet ef database update `
  --project projects/Oxygen.Editor.Data/src `
  --startup-project projects/Oxygen.Editor.Data/src `
  --framework net9.0 `
  -- --db-path="F:\path\to\PersistentState.db"
```

**Apply to specific migration (not latest):**

```powershell
dotnet ef database update <MigrationName> `
  --project projects/Oxygen.Editor.Data/src `
  --startup-project projects/Oxygen.Editor.Data/src `
  --framework net9.0 `
  -- --db-path="F:\path\to\PersistentState.db"
```

### Removing Migrations

**Remove the last migration (not yet applied):**

```powershell
dotnet ef migrations remove `
  --project projects/Oxygen.Editor.Data/src `
  --startup-project projects/Oxygen.Editor.Data/src
```

**Warning:** This only works if the migration hasn't been applied to any database. If applied, you must first roll back the database (see [Rolling Back Migrations](#rolling-back-migrations)).

### Rolling Back Migrations

**Revert database to a previous migration:**

```powershell
dotnet ef database update <PreviousMigrationName> `
  --project projects/Oxygen.Editor.Data/src `
  --startup-project projects/Oxygen.Editor.Data/src `
  --framework net9.0 `
  -- --db-path="F:\path\to\PersistentState.db"
```

**Revert to initial state (remove all migrations):**

```powershell
dotnet ef database update 0 `
  --project projects/Oxygen.Editor.Data/src `
  --startup-project projects/Oxygen.Editor.Data/src `
  --framework net9.0 `
  -- --db-path="F:\path\to\PersistentState.db"
```

## Migration Management

### Listing Migrations

**View all migrations and their status:**

```powershell
dotnet ef migrations list `
  --project projects/Oxygen.Editor.Data/src `
  --startup-project projects/Oxygen.Editor.Data/src
```

Output shows:

- Migration name
- Migration ID
- Whether it's been applied to the database

### Generating SQL Scripts

**Generate SQL for review or manual execution:**

```powershell
# All migrations
dotnet ef migrations script `
  --project projects/Oxygen.Editor.Data/src `
  --startup-project projects/Oxygen.Editor.Data/src `
  -o migration.sql
```

**From specific migration to latest:**

```powershell
dotnet ef migrations script <FromMigration> `
  --project projects/Oxygen.Editor.Data/src `
  --startup-project projects/Oxygen.Editor.Data/src `
  -o migration.sql
```

**Between two migrations:**

```powershell
dotnet ef migrations script <FromMigration> <ToMigration> `
  --project projects/Oxygen.Editor.Data/src `
  --startup-project projects/Oxygen.Editor.Data/src `
  -o migration.sql
```

**Idempotent script (safe to run multiple times):**

```powershell
dotnet ef migrations script --idempotent `
  --project projects/Oxygen.Editor.Data/src `
  --startup-project projects/Oxygen.Editor.Data/src `
  -o migration.sql
```

### Migration Naming Conventions

Use descriptive, action-oriented names:

**Good examples:**

- `AddTemplateUsageTable`
- `RenameContentBrowserStateColumn`
- `AddIndexToProjectLocation`
- `IncreasePathLengthLimit`

**Bad examples:**

- `Update1`
- `Changes`
- `NewMigration`

## Database Operations

### Creating Initial Database

**The database is created automatically** when the application runs for the first time via:

```csharp
context.Database.Migrate();
```

If you need to manually create the database:

```powershell
dotnet ef database update `
  --project projects/Oxygen.Editor.Data/src `
  --startup-project projects/Oxygen.Editor.Data/src `
  --framework net9.0 `
  -- --db-path="F:\path\to\PersistentState.db"
```

### Dropping and Recreating Database

**Warning: This destroys all data!**

```powershell
# Drop database
dotnet ef database drop `
  --project projects/Oxygen.Editor.Data/src `
  --startup-project projects/Oxygen.Editor.Data/src `
  -- --db-path="F:\path\to\PersistentState.db"

# Recreate with all migrations
dotnet ef database update `
  --project projects/Oxygen.Editor.Data/src `
  --startup-project projects/Oxygen.Editor.Data/src `
  --framework net9.0 `
  -- --db-path="F:\path\to\PersistentState.db"
```

### Database Location

The database file location is determined by:

1. **Development mode (`--mode=dev`)**: `PathFinder` resolves to development data directory
2. **Production mode (`--mode=real`)**: `PathFinder` resolves to production data directory
3. **Explicit path (`--db-path`)**: Use specified path directly

**Finding your database:**

```csharp
// In code
var pathFinder = new PathFinder(fileSystem, PathFinderMode.Dev);
var dbPath = pathFinder.GetEditorDataPath() / "PersistentState.db";
```

## Troubleshooting

### Common Errors and Solutions

#### "A suitable provider was not found"

**Cause:** SQLite provider not configured

**Solution:**

```powershell
dotnet add package Microsoft.EntityFrameworkCore.Sqlite
```

Ensure `DbContextOptionsBuilder` uses `.UseSqlite()`:

```csharp
options.UseSqlite($"Data Source={dbPath}");
```

#### "Unable to create an object of type 'PersistentState'"

**Cause:** Missing design-time factory or incorrect configuration

**Solution:**

1. Verify `DesignTimePersistentStateFactory` exists
2. Ensure arguments are passed correctly after `--`
3. Try explicitly specifying `--db-path`

#### "The project file is invalid or doesn't exist"

**Cause:** Incorrect path in `--project` or `--startup-project`

**Solution:** Use absolute paths or paths relative to repository root:

```powershell
--project projects/Oxygen.Editor.Data/src/Oxygen.Editor.Data.csproj
```

#### "Build failed"

**Cause:** Code compilation errors

**Solution:**

1. Build the project directly: `dotnet build projects/Oxygen.Editor.Data/src`
2. Fix any compilation errors
3. Retry migration command

#### File locking on SQLite

**Cause:** Database file open in another process

**Solution:**

1. Close any SQLite browsers or editors
2. Stop running application instances
3. Use `--use-in-memory-db` for migration generation

#### Migration Already Applied

**Cause:** Attempting to remove a migration that's been applied

**Solution:**

1. Roll back to previous migration first:

   ```powershell
   dotnet ef database update <PreviousMigration> ...
   ```

2. Then remove migration:

   ```powershell
   dotnet ef migrations remove ...
   ```

## Best Practices

### Development Workflow

1. **Always create migrations in isolation** — Make one logical change per migration
2. **Test migrations on a copy** — Never test destructive migrations on production data
3. **Review generated SQL** — Use `migrations script` to verify changes
4. **Use descriptive names** — Future developers will thank you
5. **Commit migrations with code changes** — Keep them in sync

### Schema Changes

#### Safe Operations

- Adding nullable columns
- Adding new tables
- Adding indexes
- Increasing string length limits

#### Operations Requiring Care

**Adding non-nullable columns:**

```csharp
// Step 1: Add as nullable with default
migrationBuilder.AddColumn<string>(
    name: "NewColumn",
    table: "TableName",
    nullable: true,
    defaultValue: "DefaultValue");

// Step 2: Populate data
migrationBuilder.Sql("UPDATE TableName SET NewColumn = 'Value' WHERE NewColumn IS NULL");

// Step 3 (in next migration): Make non-nullable
migrationBuilder.AlterColumn<string>(
    name: "NewColumn",
    table: "TableName",
    nullable: false);
```

**Renaming columns:**

```csharp
migrationBuilder.RenameColumn(
    name: "OldName",
    table: "TableName",
    newName: "NewName");
```

**Dropping columns:**

```csharp
// Document why data is being deleted!
// Consider backing up data first
migrationBuilder.DropColumn(
    name: "ColumnName",
    table: "TableName");
```

### Collaboration

- **Pull before migrating** — Ensure you have latest migrations
- **Coordinate schema changes** — Discuss breaking changes with team
- **Test migration rollback** — Verify `Down()` method works
- **Document complex migrations** — Add comments explaining non-obvious logic

### CI/CD Integration

For automated builds:

```powershell
# Generate SQL script in CI pipeline
dotnet ef migrations script --idempotent `
  --project projects/Oxygen.Editor.Data/src `
  --startup-project projects/Oxygen.Editor.Data/src `
  -o artifacts/migration.sql `
  -- --use-in-memory-db
```

## Advanced Scenarios

### Custom Migration Code

Add custom SQL or seed data in migrations:

```csharp
protected override void Up(MigrationBuilder migrationBuilder)
{
    // Generated schema changes
    migrationBuilder.CreateTable(...);

    // Custom seed data
    migrationBuilder.InsertData(
        table: "Settings",
        columns: new[] { "ModuleName", "Key", "JsonValue" },
        values: new object[] { "Editor", "Version", "\"1.0.0\"" });

    // Custom SQL
    migrationBuilder.Sql(@"
        CREATE TRIGGER UpdateTimestamp
        AFTER UPDATE ON Settings
        BEGIN
            UPDATE Settings SET UpdatedAt = datetime('now') WHERE Id = NEW.Id;
        END;
    ");
}
```

### Multiple Databases

To maintain different databases for different environments:

```powershell
# Development
dotnet ef database update -- --mode=dev

# Testing
dotnet ef database update -- --db-path="TestData\test.db"

# Production (manual)
dotnet ef migrations script --idempotent -o prod-migration.sql
# Then apply prod-migration.sql via deployment process
```

### Data Migration

For complex data transformations:

```csharp
protected override void Up(MigrationBuilder migrationBuilder)
{
    // Add new column
    migrationBuilder.AddColumn<string>("NewColumn", "TableName", nullable: true);

    // Transform data using raw SQL
    migrationBuilder.Sql(@"
        UPDATE TableName
        SET NewColumn = CASE
            WHEN OldColumn = 'A' THEN 'TypeA'
            WHEN OldColumn = 'B' THEN 'TypeB'
            ELSE 'Unknown'
        END;
    ");

    // Make non-nullable after population
    migrationBuilder.AlterColumn<string>("NewColumn", "TableName", nullable: false);

    // Drop old column
    migrationBuilder.DropColumn("OldColumn", "TableName");
}
```

### Performance Optimization

Monitor query performance:

```csharp
// Enable sensitive data logging in development
optionsBuilder
    .UseSqlite(connectionString)
    .EnableSensitiveDataLogging()
    .EnableDetailedErrors()
    .LogTo(Console.WriteLine, LogLevel.Information);
```

Add indexes via migrations:

```csharp
migrationBuilder.CreateIndex(
    name: "IX_ProjectsUsage_LastUsedOn",
    table: "ProjectsUsage",
    column: "LastUsedOn",
    descending: true);
```

## Repository-Specific Notes

### Artifacts Output

This repository uses .NET 8+ simplified artifacts output (`UseArtifactsOutput=true`):

- All build outputs and intermediate files are under `artifacts/` at repository root
- Structure: `artifacts/{bin|obj|package|publish}/<ProjectName>/<pivot>/`
- The SDK automatically manages intermediate paths, no manual `--msbuildprojectextensionspath` configuration needed

See [`tooling/ARTIFACTS-README.md`](../../../tooling/ARTIFACTS-README.md) for details.

### Multi-Targeting

The project targets both `net9.0` and `net9.0-windows10.0.26100.0`. When running EF tools, specify:

```powershell
--framework net9.0
```

### Path Conventions

All commands assume repository root is `f:\projects\DroidNet`. Adjust paths for your environment.

## Additional Resources

- [EF Core Documentation](https://learn.microsoft.com/ef/core/)
- [EF Core CLI Reference](https://learn.microsoft.com/ef/core/cli/dotnet)
- [SQLite Documentation](https://www.sqlite.org/docs.html)
- [Data Model Documentation](./data-model.md)
- [Project README](../README.md)
