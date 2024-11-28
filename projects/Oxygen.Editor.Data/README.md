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

Using an in-memory SQLite database:

```shell
dotnet ef migrations add MIGRATION__NAME --framework net8.0 --msbuildprojectextensionspath ..\..\..\obj\projects\Oxygen.Editor.Data\src\ -- --use-in-memory-db
```

Using a file-based SQLite database in development mode:

```shell
dotnet ef migrations add MIGRATION__NAME --framework net8.0 --msbuildprojectextensionspath ..\..\..\obj\projects\Oxygen.Editor.Data\src\ -- --mode=dev
```

### Reviewing the Migration

Review the generated migration code to ensure it accurately reflects the intended changes.

```csharp
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

```csharp
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

```csharp
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

## Settings Management

### SettingsManager

The `SettingsManager` class manages the persistence and retrieval of module settings using a database context. It provides methods to save and load settings for different modules, using a `PersistentState` context to interact with the database and caching settings in memory to improve performance. The settings are serialized and deserialized using JSON.

### Defining Settings

Settings destined for the persistent state database can be defined using the `ModuleSettings` base class. Properties marked with the `PersistedAttribute` will be saved and loaded by the `SettingsManager`.

```csharp
public class MyModuleSettings : ModuleSettings
{
    [Persisted]
    public int MySetting { get; set; }

    public MyModuleSettings(SettingsManager settingsManager, string moduleName)
        : base(settingsManager, moduleName)
    {
    }
}
```

### Loading and Saving Settings

To load and save settings, use the `LoadAsync` and `SaveAsync` methods provided by the `ModuleSettings` class.

```csharp
// Initialize the PersistentState context
var options = new DbContextOptionsBuilder<PersistentState>()
    .UseSqlite("Data Source=app.db")
    .Options;
var context = new PersistentState(options);

// Create the SettingsManager
var settingsManager = new SettingsManager(context);

// Create the module settings
var mySettings = new MyModuleSettings(settingsManager, "MyModule");

// Load the settings
await mySettings.LoadAsync();

// Modify a setting
mySettings.MySetting = 42;

// Save the settings
await mySettings.SaveAsync();
```

## Template Usage Data Service

The `TemplateUsageService` class provides services for managing template usage data, including retrieving, updating, and validating template usage records. It uses an `IMemoryCache` to cache template usage data for improved performance.

### Example Usage

Here is an example of how to use the `TemplateUsageService`:

```csharp
var contextOptions = new DbContextOptionsBuilder<PersistentState>()
    .UseInMemoryDatabase(databaseName: "TestDatabase")
    .Options;
var context = new PersistentState(contextOptions);
var cache = new MemoryCache(new MemoryCacheOptions());
var service = new TemplateUsageService(context, cache);

// Add or update a template usage record
await service.UpdateTemplateUsageAsync("Location1");

// Retrieve the most recently used templates
var recentTemplates = await service.GetMostRecentlyUsedTemplatesAsync();
foreach (var template in recentTemplates)
{
    Console.WriteLine($"Template Location: {template.Location}, Last Used: {template.LastUsedOn}");
}

// Get a specific template usage record
var templateUsage = await service.GetTemplateUsageAsync("Location1");
if (templateUsage != null)
{
    Console.WriteLine($"Template Location: {templateUsage.Location}, Times Used: {templateUsage.TimesUsed}");
}
else
{
    Console.WriteLine("Template not found.");
}
```

## Project Usage Data Service

The `ProjectUsageService` class provides services for managing project usage data, including retrieving, updating, and validating project usage records. It uses an `IMemoryCache` to cache project usage data for improved performance.

### Example Usage

Here is an example of how to use the `ProjectUsageService`:

```csharp
var contextOptions = new DbContextOptionsBuilder<PersistentState>()
    .UseInMemoryDatabase(databaseName: "TestDatabase")
    .Options;
var context = new PersistentState(contextOptions);
var cache = new MemoryCache(new MemoryCacheOptions());
var service = new ProjectUsageService(context, cache);

// Add or update a project usage record
await service.UpdateProjectUsageAsync("Project1", "Location1");

// Retrieve the most recently used projects
var recentProjects = await service.GetMostRecentlyUsedProjectsAsync();
foreach (var project in recentProjects)
{
    Console.WriteLine($"Project: {project.Name}, Last Used: {project.LastUsedOn}");
}

// Update the content browser state
await service.UpdateContentBrowserStateAsync("Project1", "Location1", "NewState");

// Rename or move a project
await service.UpdateProjectNameAndLocationAsync("Project1", "Location1", "NewProject1", "NewLocation1");
```
