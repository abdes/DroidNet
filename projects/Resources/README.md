# DroidNet.Resources

DroidNet.Resources is a lightweight .NET library that provides extension helpers to perform resource localization in Windows App SDK applications. This library simplifies retrieving localized strings from application and assembly resource maps while providing safe, well-defined fallbacks.

---

## Quick start

Install the library via NuGet:

```pwsh
dotnet add package DroidNet.Resources
```

Add a reference to the runtime `DroidNet.Resources` package in any project where you need to retrieve localized strings.

---

## Features

- Simple extension methods that look up keys under `Localized` subtrees of resource maps
- Multiple lookup strategies (app default map, per-assembly map, explicit assembly map)
- Graceful fallback behavior — if a key isn’t found, the original input string is returned
- Designed to be used from standard .NET projects and in contexts that may use AOT/trimming

---

## Installation

Install via NuGet:

```pwsh
dotnet add package DroidNet.Resources
```

---

## Usage

### Scenario

The examples below use the concrete resource maps described here so the behavior is unambiguous.

- **Application resource map** (`Localized` subtree):
  - `Localized/MSG_Hello` -> "Hello from MyApp"
  - `Localized/MSG_Goodbye` -> "Goodbye from MyApp"
  - `Special/Localized/MSG_Special` -> "Special message from MyApp"

- **LibraryX assembly resource map** (returned by `ResourceMapProvider.GetAssemblyResourceMap(typeof(LibraryX.SomeType).Assembly)`):
  - `LibraryX/Localized/MSG_Hello` -> "Hello from LibraryX"
  - `LibraryX/Localized/MSG_Thanks` -> "Thanks"
  - `LibraryX/Special/Localized/SPEC_Feature` -> "Feature text from LibraryX (special map)"

- **PluginY assembly resource map** (example PRI next to the plugin binary):
  - `PluginY/Localized/MSG_Goodbye` -> "Goodbye from PluginY"

You can obtain an assembly resource map in code like this:

```csharp
var xMap = ResourceMapProvider.GetAssemblyResourceMap(typeof(LibraryX.SomeType).Assembly);
```

Note: the `IResourceMap` returned for an assembly is a single map object that can contain several
child subtrees (for example `Localized`, `Special`, etc.). You can access those subtrees directly:

```csharp
var xMap = ResourceMapProvider.GetAssemblyResourceMap(typeof(LibraryX.SomeType).Assembly);
// Localized subtree (used by `GetLocalized` and the `GetLocalized` assembly overload)
var localized = xMap.GetSubtree("Localized");
// A different subtree shipped with the assembly
var special = xMap.GetSubtree("Special");
```

### GetLocalized Extension Methods

The library exposes three overloads for `GetLocalized` to make common lookup patterns easy while
keeping lookups explicit and robust:

```csharp
// App-level/default map
public static string GetLocalized(this string value, IResourceMap? resourceMap = null)
{
    ...
}

// Generic overload targeting the assembly of type T
public static string GetLocalized<T>(this string value, IResourceMap? resourceMap = null)
{
    ...
}

// Assembly overload: explicit assembly provided
public static string GetLocalized(this string value, Assembly assembly, IResourceMap? resourceMap = null)
{
    ...
}
```

Descriptions:

- `GetLocalized(this string value, IResourceMap? resourceMap = null)` — Default behavior. Uses the application's
  default resource map (the app-level `Localized` subtree) unless `resourceMap` is supplied. If the key isn't found
  the method returns the original string.
- `GetLocalized<T>(this string value, IResourceMap? resourceMap = null)` — Generic convenience overload that
  targets `typeof(T).Assembly` as the target assembly's resource map. This avoids `typeof(T).Assembly` boilerplate
  at call sites and is strongly typed (no runtime ambiguity).
- `GetLocalized(this string value, Assembly assembly, IResourceMap? resourceMap = null)` — Explicit assembly
  overload: use this when the target assembly is provided at runtime or when you prefer explicitness.

Behavior note: all `resourceMap` parameters are treated as a top-level map and the helpers always look under the
`Localized` child of any `resourceMap` you pass. Do NOT pass the `Localized` subtree itself (for example
`xMap.GetSubtree("Localized")`) — the helpers will attempt `Localized/Localized/KEY` which will fail.

When supplying a `resourceMap` argument to any `GetLocalized` overload, the
helpers always look for a `Localized` child under the map you provide. That means callers must be
careful which map they pass:

- If you pass the assembly's top-level resource map (for example the value returned by
  `ResourceMapProvider.GetAssemblyResourceMap(...)`), the helpers will use its `Localized` child
  (equivalent to calling `assemblyMap.GetSubtree("Localized")`).
- If you pass a subtree that itself contains a `Localized` child (for example
  `xMap.GetSubtree("Special")` where `Special/Localized/...` exists), the helpers will look under
  that subtree's `Localized` child.

Do NOT pass the `Localized` subtree itself (for example `xMap.GetSubtree("Localized")`). The
helpers will always attempt to access a `Localized` child of whatever map you provide; passing the
`Localized` subtree causes the helpers to attempt `Localized/Localized/KEY` lookups (which will
fail).

```csharp
// App-level resolution
"MSG_Hello".GetLocalized(); // => "Hello from MyApp"
"MSG_Goodbye".GetLocalized(); // => "Goodbye from MyApp"

// App-specific resource map (using the App assembly)
var aMap = ResourceMapProvider.GetAssemblyResourceMap(typeof(MyApp.SomeType).Assembly);
var sMap = aMap.GetSubtree("Special");
"MSG_Special".GetLocalized(sMap); // => "Special message from MyApp"
"MSG_Hello".GetLocalized(sMap); // => "Hello from MyApp" (fallback to app)
```

### GetLocalized (Assembly overload) — lookup order

This is an explanation of the lookup order when `GetLocalized` targets a specific assembly using the
`GetLocalized(this string value, Assembly assembly, IResourceMap? resourceMap = null)` overload.

Lookup algorithm (in priority order):

- **Provided resource map — `Localized` subtree**: If a `resourceMap` argument is provided, search its
  `Localized` subtree for the key first. If `resourceMap` is null, perform the later steps in the order below.
- **Assembly's own resource map — `Localized` subtree**: Attempt to obtain the assembly's own resource map via
  `ResourceMapProvider.GetAssemblyResourceMap(assembly)` and search its `Localized` subtree. This covers resources
  shipped with the assembly itself (including a local `*.pri` next to the binary).
- **Application default resource map — `Localized` subtree (app fallback)**: If the key was not found in the
  provided or assembly maps, search the application's default resource map's `Localized` subtree as the final
  application-level fallback.
- **Final fallback**: If all lookups fail, return the original input string unchanged.

#### Example: happy lookup no map (assembly overload)

```csharp
// No explicit resourceMap supplied. The method will check the assembly's own resource map
// (via ResourceMapProvider.GetAssemblyResourceMap) before falling back to the app map.
var assembly = typeof(LibraryX.SomeType).Assembly;
"MSG_Hello".GetLocalized(assembly); // => "Hello from LibraryX"
```

#### Example: subtree (`Special`) from an assembly map

```csharp
var assembly = typeof(LibraryX.SomeType).Assembly;
var xMap = ResourceMapProvider.GetAssemblyResourceMap(typeof(LibraryX.SomeType).Assembly);
var sMap = xMap.GetSubtree("Special");
"SPEC_Feature".GetLocalized(assembly, sMap); // => "Feature text from LibraryX (Special map)"
```

#### Example: assembly fallback (provided map misses key)

```csharp
var assembly = typeof(LibraryX.SomeType).Assembly;
var xMap = ResourceMapProvider.GetAssemblyResourceMap(typeof(LibraryX.SomeType).Assembly);
var sMap = xMap.GetSubtree("Special");
"MSG_Thanks".GetLocalized(assembly, sMap); // => "Thanks"
```

#### Example: application fallback

```csharp
// When neither the provided map nor the assembly's own map contain the key, the app map is used.
var assembly = typeof(LibraryX.SomeType).Assembly;
var sMap = xMap.GetSubtree("Special");
"MSG_Goodbye".GetLocalized(assembly); // => "Goodbye from MyApp"
"MSG_Goodbye".GetLocalized(assembly, sMap); // => "Goodbye from MyApp"
```

#### Example: assembly-local override (PluginY)

```csharp
// PluginY ships a PRI next to the plugin binary; its own map contains:
// PluginY/Localized/MSG_Goodbye -> "Goodbye from PluginY"
var pluginAssembly = typeof(PluginY.SomeType).Assembly;
"MSG_Goodbye".GetLocalized(pluginAssembly); // => "Goodbye from PluginY"
```

---

## Generated helpers & `L` shortcut (source generator)

To reduce repetition you can add a small per-assembly helper class to expose a short `L` helper method or use a
source generator included in this repository that will generate one automatically for any assembly that references the
resources package.

Example of a small manually added helper in an assembly:

```csharp
public static class Localized
{
    private static readonly Assembly _assembly = typeof(Localized).Assembly;

    public static string L(this string value, IResourceMap? resourceMap = null)
        => value.GetLocalized(_assembly, resourceMap);
}
```

Usage:

```csharp
"MSG_Hello".L(); // => "Hello from my assembly"
```

### Source generator approach

- If you prefer a zero-copy approach, enable the `DroidNet.Resources.Generator` source generator.
- When the generator runs, it will add a generated per-assembly helper (for example `Res` or `Localized`) that exposes
  a short `L(this string value, IResourceMap? resourceMap = null)` extension and a few other convenience helpers.
- The generated helper is typed and bound to the assembly the generator runs in, ensuring no runtime ambiguity.

Advantages of the generator approach:

- Zero call-site boilerplate: `"MSG_Hello".L()` works everywhere in your assembly.
- Compile-time binding: avoids `GetCallingAssembly` pitfalls and runtime ambiguity.
- Works in trimmed/AOT builds since the assembly is resolved at compile-time.

To add the generator, reference the generator package (if published) or add the generator project and configure your
project to use it. The generator will produce the helper during compilation; no changes are needed in code files.

---

## Testing

Unit tests for the runtime library are located under `projects/Resources/tests`. Run them with:

```pwsh
dotnet test projects\Resources\tests\Resources.Tests.csproj -c Debug
```

---

## License

Distributed under the MIT License. See `LICENSE` in the repository root.
