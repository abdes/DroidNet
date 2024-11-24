# DroidNet.Resources

DroidNet.Resources is a .NET library that provides extension methods for resource localization. This
library simplifies the process of retrieving localized strings in Windows App SDK applications.

## Features

- Retrieve localized strings from the application's main resource map or from the calling assembly's resource map.
- Handle localization failures gracefully.

## Installation

To install DroidNet.Resources, add the following package to your project:

```sh
dotnet add package DroidNet.Resources
```

## Usage

### GetLocalized Extension Method

The `GetLocalized` extension method tries to find the resource in the main application resource map,
which can be overridden by providing the resource map to use as a parameter. If the resource is not
found, it returns the original string.

```csharp
using DroidNet.Resources;

string originalString = "Hello";
string localizedString = originalString.GetLocalized();
```

### GetLocalizedMine Extension Method

The `GetLocalizedMine` extension method to find the resource in a child resource map of the main
application resource map or the one given as a parameter, by using the calling assembly name. This
is particularly useful to get localized strings from a specific assembly.

```csharp
using DroidNet.Resources;

string originalString = "Hello";
string localizedString = originalString.GetLocalizedMine();
```
