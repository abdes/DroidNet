# DroidNet.Resources.Generator

Source generator that emits a lightweight, per-assembly localization helper for `DroidNet.Resources`.

This generator produces an internal, per-assembly `Localized` helper that lives in a stable generator namespace (`DroidNet.Resources.Generator.Localized_<hash>`) and adds small extension helpers on `string` such as `L()` and `R()`. The extensions forward to the runtime `DroidNet.Resources.ResourceExtensions` methods and are assembly-bound via an internal assembly marker so you can write `"Hello".L()` or `"Subtree/KEY".R()` without boilerplate.

---

## Quick start

1. Reference the runtime `DroidNet.Resources` library in the project you want localization helpers in. The generator only runs if the runtime `ResourceExtensions` type is available in the compilation.
2. Consume `DroidNet.Resources.Generator` as an analyzer. There are three common ways to add the generator to a project (see the next section for explicit snippets):
3. Build the project — the generator will add an internal `Localized` helper into your project's generated, per-assembly namespace (see Key behaviors for details).

### Add to your project (examples)

Add the runtime and generator to your project using any of the following approaches.

Runtime: Project reference (local development)

```xml
<ItemGroup>
  <ProjectReference Include="..\..\..\projects\Resources\src\Resources.csproj" />
</ItemGroup>

```

Runtime: NuGet package (published runtime)

```xml
<ItemGroup>
  <PackageReference Include="DroidNet.Resources" Version="1.0.0" />
</ItemGroup>

```

Generator: NuGet analyzer package (typical consumer)

```xml
<ItemGroup>
  <PackageReference Include="DroidNet.Resources.Generator" Version="1.0.0" />
</ItemGroup>

```

Generator: Local project analyzer reference (development)

```xml
<ItemGroup>
  <ProjectReference Include="..\..\..\projects\Resources.Generator\src\Resources.Generator.csproj" OutputItemType="Analyzer" ReferenceOutputAssembly="false" />
</ItemGroup>

```

Generator: Reference a built analyzer DLL directly

```xml
<ItemGroup>
  <Analyzer Include="..\path\to\DroidNet.Resources.Generator.dll" />
</ItemGroup>

```

Example usage after generation:

```csharp
using DroidNet.Resources; // runtime APIs (IResourceMap, ResourceExtensions)

string label = "MSG_Hello";
// Use generated helper (assembly-bound via generated `Localized` helper):
var localized = label.L();

// The generated helper also includes a path helper `R` for subtree lookups:
var secret = "Special/MSG_SecretMessage".R();
// Optionally pass a resource map to either API:
var localizedWithMap = label.L(resourceMap);
```

---

## Key behaviors

- Generation is performed only when the project references the runtime API `DroidNet.Resources.ResourceExtensions`; otherwise the generator does nothing for that project.
- The generated `Localized` helper type is placed in a stable generator namespace `DroidNet.Resources.Generator.Localized_<shortHash>`, where `<shortHash>` is an 8-character hex value computed from the assembly name. This keeps helpers per-assembly and avoids collisions.
- If the project already defines a `Localized` type with the exact same fully-qualified name, and that type is a user-defined source type (not marked generated), the generator will skip generation to prevent colliding with user code.
- Any internal generator exceptions are surfaced as `DNRESGEN001` error diagnostics; in that case no generated sources are produced.

### DI extension generation policy

- The generator will add a DI extension (`LocalizationContainerExtensions`) only for executable output kinds (Console, Windows, WindowsRuntime), not for libraries.
- The DI extension is emitted only when the `DryIoc.IContainer` symbol is present in project references. If `DryIoc` is not present, the generator will skip the DI extension and emit `DNRESGEN007`.
- The generated `LocalizationContainerExtensions` class lives in the `DroidNet.Resources` namespace and exposes `WithLocalization(this DryIoc.IContainer container)`, which registers the default `DefaultResourceMapProvider` (only if nothing else is registered) and calls `ResourceExtensions.Initialize(provider)`.

Example: Using the generated DI extension with DryIoc

```csharp
using DryIoc;
using DroidNet.Resources;

var container = new Container();

// Registers the default provider if nothing else has been registered and
// ensures ResourceExtensions is initialized for localizations used by the app.
container.WithLocalization();

// Once initialized, you can resolve values normally and use generated helpers
// such as "MSG_Hello".L()
var localized = "MSG_Hello".L();
```

This policy prevents generating code that depends on `DryIoc` in projects that don't reference it, avoiding avoidable compile-time errors while keeping the generator useful for executable projects that use `DryIoc`.

### Opt-out

You can opt out of the generator using an MSBuild property:

```xml
<PropertyGroup>
  <DroidNetResources_GenerateHelper>false</DroidNetResources_GenerateHelper>
</PropertyGroup>
```

This will prevent the generator from producing the helper.

Providing any value other than `false`/`0` keeps generation enabled; the property is treated as opt-in only when explicitly disabled.

### Diagnostics and debugging

The generator emits helpful diagnostics to surface generator activity and failures. Important diagnostic IDs:

- `DROIDRES001` (Info): Generator started processing an assembly.
- `DROIDRES002` (Info): Generation complete for an assembly — reports how many files were generated.
- `DNRESGEN001` (Error): Generator failure — an unhandled exception occurred. The message contains the exception text.
- `DNRESGEN002` (Info): Whether the runtime type `DroidNet.Resources.ResourceExtensions` was found in the compilation.
- `DNRESGEN003` (Info): A `Localized` type with the expected name already exists.
- `DNRESGEN004` (Info): Skipping generation because the `Localized` type is user-defined.
- `DNRESGEN005` (Info): The generator detected a previously generated helper and is regenerating for a clean build.
- `DNRESGEN006` (Info): No existing helper found — the generator will proceed to create one.
- `DNRESGEN007` (Info): `DryIoc.IContainer` was not found in the compilation; DI extension will not be generated.

If a generator failure occurs, it emits a diagnostic rather than crashing the compiler; this ensures errors surface and builds using the generator can be diagnosed without corrupting the build process.

---

## Implementation notes (for maintainers)

- The generator is implemented as an `IIncrementalGenerator` (`ResourcesGenerator`), and checks for a referenced `DroidNet.Resources` type to decide if it should run.
- The generator intentionally avoids using project root namespace and instead derives the generated namespace from a short SHA-256-based hash over the assembly name. This keeps namespaces stable and collision-resistant without depending on project root-namespace values.
- The generated source is added with stable hint-name formats:
  - `Localization_{AssemblyName}.g.cs` — contains the `Localized` helper (namespace `DroidNet.Resources.Generator.Localized_<hash>`). The `AssemblyName` is used verbatim and may contain dots (for example `DroidNet.Resources.Sample`).
  - `ResourceExtensions_{AssemblyName}.g.cs` — contains the DI extension helpers (only emitted for executable output kinds). The `AssemblyName` is used verbatim.
- An internal `AssemblyMarker` type and a `Localized` helper that binds lookups to `typeof(AssemblyMarker).Assembly`.
- A `ResourcesBootstrap` class with a `ModuleInitializer` (`InitializeLocalization`) that ensures a default provider is registered via `ResourceExtensions.Initialize(new DefaultResourceMapProvider())`.
- Extension helpers on `string`:
  - `public static string L(this string value, IResourceMap? resourceMap = null)` which forwards to `ResourceExtensions.L<T>` bound to `AssemblyMarker`.
  - `public static string R(this string path)` which forwards to `ResourceExtensions.R<T>` bound to `AssemblyMarker`.

---

## Building & running the sample

This repo includes a minimal sample app under `samples/` that demonstrates using the generator.

```pwsh
# Build the sample
dotnet build projects\Resources.Generator\samples\src\Resources.Generator.Sample.csproj -c Debug
# Run the sample
dotnet run --project projects\Resources.Generator\samples\src\Resources.Generator.Sample.csproj -c Debug
```

---

## Tests

Unit tests are available under `tests/Unit`. Run them with `dotnet test`.

```pwsh
dotnet test projects\Resources.Generator\tests\Unit\Resources.Generator.Tests.csproj -c Debug
```

---

## Packaging notes

- The project is an analyzer package; packaging ensures the analyzer DLLs are emitted under `analyzers/dotnet/cs`.
- The project uses `IsRoslynComponent` and `PackageType` to identify itself as an analyzer generator.
- We deliberately set `IncludeBuildOutput` to `false` to avoid placing the generator assembly under the regular `lib/` folder — it's consumed as an analyzer.

---

## License

Distributed under the MIT License. See `LICENSE` in the repository root.
