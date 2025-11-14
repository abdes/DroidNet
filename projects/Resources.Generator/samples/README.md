# Resources.Generator sample app

This minimal sample demonstrates using the `DroidNet.Resources.Generator` source generator. It shows how to:

- Reference the generator as an analyzer project reference
- Reference the runtime `DroidNet.Resources` project so the generator picks up `ResourceExtensions`
- Use the generated `L()` extension method from the generated namespace

To build and run the sample:

```pwsh
dotnet build projects\Resources.Generator\samples\src\Resources.Henerator.Sample.csproj -c Debug
dotnet run --project projects\Resources.Generator\samples\src\Resources.Henerator.Sample.csproj -c Debug
```
