# Oxygen Editor Data Layer

This README is a beginner-friendly A→Z guide to using Entity Framework Core (EF Core) with this project. It explains how to install the tools, create the initial database schema, add and apply migrations, and common troubleshooting tips.

> Quick note: this project includes a design-time factory (`DesignTimePersistentStateFactory`) that accepts extra command-line arguments used by the `dotnet ef` tools. The factory supports:
>
> - `--mode` — `dev` or `real` (resolves where the on-disk SQLite file lives). Default: `dev`.
> - `--use-in-memory-db` — use an in-memory SQLite instance (useful for tests and creating migrations without touching the local file).
> - `--db-path` — specify an explicit SQLite database file path to use for design-time operations (this takes precedence over `--mode`).
>
> Only one of these options should be provided at a time. If both are provided the factory throws an `ArgumentException`.

## Prerequisites

- Install the .NET SDK (9.0 or later) and ensure `dotnet` is on your PATH. Verify with:

 `dotnet --version`

- Install the EF Core CLI tool (global tool):

 `dotnet tool install --global dotnet-ef`

 If you already installed it earlier, update it with:

 `dotnet tool update --global dotnet-ef`

- Ensure the EF Core design and provider packages are referenced by the `Oxygen.Editor.Data` project. From the project folder (`projects/Oxygen.Editor.Data/src`) run (if not already present):

 `dotnet add package Microsoft.EntityFrameworkCore.Design`

 `dotnet add package Microsoft.EntityFrameworkCore.Sqlite`

 The `Design` package is required for `dotnet ef` design-time support; the `Sqlite` package provides the SQLite provider used by the project.

## Out-of-source build and intermediate directories (repo root `obj` and `out`)

This repository keeps build outputs and MSBuild intermediates out-of-source at the repo root (for example `obj/` and `out/`). When running `dotnet ef`, pass an explicit `--msbuildprojectextensionspath` that points to the repo-root `obj` folder used for that project. This prevents collisions between different projects and ensures `dotnet ef` builds in the same out-of-source layout you use locally.

Example (run from the repository root, adjust paths to match your repo layout):

 `--msbuildprojectextensionspath obj/projects/Oxygen.Editor.Data/src/`

Note: you don't need to create the folder beforehand; MSBuild will create it when building. If you also use a custom `out` folder for built artifacts you can set MSBuild properties when building manually (for example when calling `dotnet build`), but `--msbuildprojectextensionspath` is usually sufficient for `dotnet ef` commands.

## Project layout notes for `dotnet ef`

`dotnet ef` needs to know two things:

- The project that contains your EF Core model and migrations (the *target* project)
- The project that should be used as the *startup* project when running design-time code (sometimes the same project)

If you run `dotnet ef` from inside the `Oxygen.Editor.Data` project folder you'll usually only need `--project` and `--startup-project` pointing to the same folder. When invoking from the repository root, pass explicit `--project` and `--startup-project` paths.

## Creating the initial migration (safe beginner workflow)

Why create the initial migration with an in-memory DB?
- Using `--use-in-memory-db` in the design-time factory avoids a file-based DB locking and makes generating the migration deterministic for schema-only changes.

From the repository root (example paths assume the repo root is the `projects` folder):

- Create an initial migration using in-memory SQLite (no DB file required):

 `dotnet ef migrations add InitialCreate --project projects/Oxygen.Editor.Data/src --startup-project projects/Oxygen.Editor.Data/src --framework net9.0 --msbuildprojectextensionspath obj/projects/Oxygen.Editor.Data/src/ -- --use-in-memory-db`

- Create an initial migration using the file-backed DB in development mode (PathFinder mode):

 `dotnet ef migrations add InitialCreate --project projects/Oxygen.Editor.Data/src --startup-project projects/Oxygen.Editor.Data/src --framework net9.0 --msbuildprojectextensionspath obj/projects/Oxygen.Editor.Data/src/ -- --mode=dev`

- Create an initial migration using an explicit database file path (useful when your DB file lives outside the default location):

 `dotnet ef migrations add InitialCreate --project projects/Oxygen.Editor.Data/src --startup-project projects/Oxygen.Editor.Data/src --framework net9.0 --msbuildprojectextensionspath obj/projects/Oxygen.Editor.Data/src/ -- --db-path="F:\\path\\to\\PersistentState.db"`

Notes:
- The `--msbuildprojectextensionspath` parameter is used in some CI / multi-project scenarios to avoid conflicting intermediate folders. The value shown above mirrors the pattern used in this repository; adjust if you run into MSBuild/SDK complaints.
- The `--` separates `dotnet ef` options from arguments forwarded to the design-time factory.

After running the command you should see a new migration folder under `projects/Oxygen.Editor.Data/src/Migrations` containing the `Up`/`Down` code.

## Reviewing and editing migrations

- Always open the generated migration class and verify that the `Up` and `Down` methods reflect the changes you expect.
- If you need to add seed data or custom SQL, you can add it to `Up()` using `migrationBuilder.InsertData(...)` or `migrationBuilder.Sql("...")`.

## Applying migrations (update the database)

To apply migrations to a database (create or update the database file), run. Use the same `--msbuildprojectextensionspath` and the same `--db-path` (if you used one when creating the migrations):

`dotnet ef database update --project projects/Oxygen.Editor.Data/src --startup-project projects/Oxygen.Editor.Data/src --framework net9.0 --msbuildprojectextensionspath obj/projects/Oxygen.Editor.Data/src/ -- --db-path="F:\\path\\to\\PersistentState.db"`

If you used the `--use-in-memory-db` mode to create migrations, switch to a file-backed mode when applying migrations in development, for example `--mode=dev` or by specifying `--db-path`.

If you want to target a specific migration instead of applying all pending migrations, pass the migration name as the last argument:

`dotnet ef database update InitialCreate --project projects/Oxygen.Editor.Data/src --startup-project projects/Oxygen.Editor.Data/src --framework net9.0 --msbuildprojectextensionspath obj/projects/Oxygen.Editor.Data/src/ -- --db-path="F:\\path\\to\\PersistentState.db"`

## Generating SQL script

To generate a SQL migration script instead of applying migrations directly (example produces `migration.sql` in the current folder):

`dotnet ef migrations script --project projects/Oxygen.Editor.Data/src --startup-project projects/Oxygen.Editor.Data/src -o ./migration.sql --msbuildprojectextensionspath obj/projects/Oxygen.Editor.Data/src/`

This produces a `migration.sql` you can inspect or run against an external DB tool.

## Common workflows

- Add a migration after changing model classes:
1. Update your POCO/DbContext model classes in code.
2. `dotent ef migrations add <DescriptiveName> --project ... --startup-project ... --msbuildprojectextensionspath obj/projects/Oxygen.Editor.Data/src/ -- --mode=dev` (or `--use-in-memory-db` for safe generation or `--db-path` to use a specific DB file).
3. Review generated migration code.
4. `dotnet ef database update --project ... --startup-project ... --msbuildprojectextensionspath obj/projects/Oxygen.Editor.Data/src/ -- --mode=dev` to apply.

- Undo the last migration (before applying to a shared DB):

 `dotnet ef migrations remove --project projects/Oxygen.Editor.Data/src --startup-project projects/Oxygen.Editor.Data/src --msbuildprojectextensionspath obj/projects/Oxygen.Editor.Data/src/`

- Revert the database to the previous migration:

 `dotnet ef database update <PreviousMigrationName> --project ... --startup-project ... --msbuildprojectextensionspath obj/projects/Oxygen.Editor.Data/src/`

## Troubleshooting

- Error: "A suitable provider was not found"
 - Ensure `Microsoft.EntityFrameworkCore.Sqlite` is installed in the `Oxygen.Editor.Data` project and that `PersistentState`'s `OnConfiguring` or the app's `DbContextOptionsBuilder` calls `.UseSqlite(...)`.

- Error: "Unable to create an object of type 'PersistentState'"
 - Ensure a design-time factory exists (this project has `DesignTimePersistentStateFactory`) or add a `IDesignTimeDbContextFactory<PersistentState>` implementation so `dotnet ef` can create your context. You can pass `--db-path` to the factory so it will use your exact DB file location.

- If `dotnet ef` complains about running in an unexpected project context, always pass explicit `--project` and `--startup-project` paths and the `--msbuildprojectextensionspath` value that matches your out-of-source layout.

- If migrations touch large binary columns or you see file locking problems on SQLite, generate migrations using `--use-in-memory-db` and only apply to the file-based DB when necessary.

## Updating model classes safely

- Adding a new nullable column: safe; new rows will have NULL or default value.
- Adding a non-nullable column: provide a default value or make it nullable first in a migration, populate values, then change to non-nullable.
- Renaming a column: write a migration that uses `RenameColumn` or uses raw SQL in `migrationBuilder.Sql(...)` to avoid data loss.
- Dropping columns: be cautious — this is destructive to existing data. Back up if data must be preserved.

## Example: model change → add migration → apply (concise example)

1. Modify `ProjectState` model in `projects/Oxygen.Editor.Data/src/Models/ProjectState.cs`.
2. From repo root run (adjust DB path to your location):

 `dotnet ef migrations add AddNewFieldToProjectState --project projects/Oxygen.Editor.Data/src --startup-project projects/Oxygen.Editor.Data/src --framework net9.0 --msbuildprojectextensionspath obj/projects/Oxygen.Editor.Data/src/ -- --db-path="F:\\path\\to\\PersistentState.db"`

3. Review `Migrations/<timestamp>_AddNewFieldToProjectState.cs`.
4. Apply the migration:

 `dotnet ef database update --project projects/Oxygen.Editor.Data/src --startup-project projects/Oxygen.Editor.Data/src --msbuildprojectextensionspath obj/projects/Oxygen.Editor.Data/src/ -- --db-path="F:\\path\\to\\PersistentState.db"`

## Safety checks and good practices

- Keep migration names descriptive (e.g. `AddTemplateUsageTable`, `RenameContentBrowserState`).
- Review migrations before committing them to version control.
- For collaborative projects, avoid generating migrations that depend on developer-local file paths; prefer in-memory generation and explicit runtime configuration.
- Use SQL scripts (`migrations script`) for DBAs or production deployments where you want to review the SQL before running it.

## Extra commands (handy)

- List migrations:

 `dotnet ef migrations list --project projects/Oxygen.Editor.Data/src --startup-project projects/Oxygen.Editor.Data/src --msbuildprojectextensionspath obj/projects/Oxygen.Editor.Data/src/`

- Remove last migration (if not applied to DB):

 `dotnet ef migrations remove --project projects/Oxygen.Editor.Data/src --startup-project projects/Oxygen.Editor.Data/src --msbuildprojectextensionspath obj/projects/Oxygen.Editor.Data/src/`

- Create ID-based SQL script from the first migration to the latest:

 `dotnet ef migrations script0 Latest --project projects/Oxygen.Editor.Data/src --startup-project projects/Oxygen.Editor.Data/src -o full-db-creation.sql --msbuildprojectextensionspath obj/projects/Oxygen.Editor.Data/src/`

## Where to learn more

- Official EF Core docs: https://learn.microsoft.com/ef/core/
- EF Core tooling: https://learn.microsoft.com/ef/core/cli/dotnet


---

If you want, I can also add a short shell script or PowerShell helper in the repo to standardize the `dotnet ef` commands (for example: `scripts/add-migration.ps1` and `scripts/update-db.ps1`) so beginners don't need to remember long command options.
