# Oxygen.Editor.Data Attributes

This assembly contains attributes used by both the runtime settings system and the compile-time source generator.

Key attribute:

- `PersistedAttribute` — annotate public properties on a `ModuleSettings` type to mark them for serialization/persistence and generator descriptor creation.

Usage:

1. Classic runtime usage (injected `EditorSettingsManager`):

```csharp
public class MyModuleSettings : ModuleSettings
{
    [Persisted]
    public int MySetting { get; set; }

    public MyModuleSettings(EditorSettingsManager settingsManager, string moduleName)
        : base(settingsManager, moduleName)
    {
    }
}
```

1. Generator-enabled usage (compile-time descriptor generation):

```csharp
public sealed partial class ExampleSettings : ModuleSettings
{
    private new const string ModuleName = "My.Module.Name";

    public ExampleSettings() : base(ModuleName) { }

    [Persisted]
    public int MySetting { get; set; }
}
```

The generator will scan for `[Persisted]` and produce typed `SettingDescriptor<T>` fields and register them with `EditorSettingsManager.StaticProvider`.

For full generator details, see `projects/Oxygen.Editor.Data/docs/source-generator.md`.
