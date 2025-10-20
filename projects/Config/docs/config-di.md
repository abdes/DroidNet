# Integrating the Config into DI

```csharp

var bootstrapper = new Bootstrapper(args)
    .Configure()
    .WithLoggingAbstraction()

    // Registers baseline Config services (SettingsManager, Encryption Providers), and add a source for `appsettings.json`
    .WithConfig()

    // Register config sources (requires WithConfig).
    // Implemented using DryIoc MadeOf, and registers services keyed by the `id`
    // Example:
    // container.Register<ISettingsSource, JsonSettingsSource>(
    //     serviceKey: id,
    //     made: Parameters.Of
    //         .Name("id", id)
    //         .Name("filePath", filePath)
    //         .Name("crypto", encryptionProvider)
    //         .Name("watch", watch)
    //
    // encryptionProvider is the resolved provider for the type passed to WithJsonConfigSource
    // );

    .WithJsonConfigSource(id: "droidnet.aura", filePath: "path/to/Aura.json", encryption: null, watch: true)
    .WithJsonConfigSource(id: "localsettings", filePath: "path/to/LocalSettings.json", encryption: null, watch: true)
    .WithJsonConfigSource(id: "secrets", filePath: "path/to/LocalSettings.json", crypto: typeof(AesEncryptionProvider), watch: false)

    // Register settings services (requires WithConfig).
    // These are the ones that will be injected into client code that needs certain settings
    // Example:
    // container.Register<ISettingsService<IAppearanceSettings>, AppearanceSettingsService>(Reuse.Singleton)

    .WithSettings<IAppearanceSettings, AppearanceSettingsService>()
    .WithSettings<IWindowDecorationSettings, WindowDecorationSettingsService>()

    .Build();
```
