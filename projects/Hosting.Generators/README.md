# Hosting Source Generators

## Overview

`Hosting.Generators` is a source generator project designed to automate the
process of registering services with a dependency injector, implemented using
the the [.Net Hosting Extensions][hosting-extensions].

Its main feature is the `InjectAs` attribute that that can be used to annotate
interfaces or classes, allowing you to specify how annotated type should be
injected.

It has the following parameters:

- `ServiceLifetime` (required): Specifies the lifetime of the service
  (Singleton, Scoped, Transient).
- `ImplementationType` (optional): Specifies the type that will be used as the
  implementation of the service. If the annotated type is a class, then this
  parameter is optional, and the type of the annotated class is used.
- `Key` (optional): A key, which will be used to register the service as a keyed
  service, and can be used later during the resolution.

## Examples

### Annotating an Interface

```csharp
[InjectAs(ServiceLifetime.Singleton, ImplementationType = typeof(MyService))]
public interface IMyService
{
    // Interface definition
}
```

In this example, MyService is registered as a singleton service of type
IMyService.

### Annotating a Class with a Default Constructor

```csharp
[InjectAs(ServiceLifetime.Scoped)]
public class MyService
{
    // Implementation
}
```

In this example, MyService is registered as a scoped service. The service type
is MyService because no ImplementationType is provided.

### Complex use cases

Sometimes you need to use a non-default constructor for your service. In this
case, you can specify constructor to use exactly like you would do when using
The ActivatorUtilities with an
[ActivatorUtilitiesConstructor][ActivatorUtilitiesConstructor] annotation.

```csharp
[InjectAs(ServiceLifetime.Transient, Key = "mine", ImplementationType = typeof(MyOtherService))]
interface IOtherService;

public class MyOtherService : IOtherService
{
    // Implementation
}

[InjectAs(ServiceLifetime.Transient)]
public class MyService
{
    private readonly IOtherService _otherService;

    [ActivatorUtilitiesConstructor]
    public MyService([FromKeyedServices("mine")] IOtherService otherService)
    {
        _otherService = otherService;
    }

    // Implementation
}
```

In this example, MyService is registered as a transient service, injected
through a non-default constructor, marked with the
`ActivatorUtilitiesConstructor` annotation. That constructor takes an
IOtherService dependency, injected using the key `"mine"`. The dependency
service for `IOtherService` is registered for the key `"mine"` with an
implementation type `MyOtherService`.

## Using this generator

To to use this generator, you need to add a reference to this project in your
.NET application. Please note that this is a source generator project, and you
need to add the reference accordingly.

```xml
<!-- Don't reference the generator dll -->
<ProjectReference Include="..\..\Hosting.Generators\src\Hosting.Generators.csproj" OutputItemType="Analyzer" ReferenceOutputAssembly="false" />
<!-- Reference the attributes project "treat as an analyzer"-->
<!-- We DO reference the attributes dll -->
<ProjectReference Include="..\..\Hosting.Generators.Attributes\src\Hosting.Generators.Attributes.csproj" OutputItemType="Analyzer" ReferenceOutputAssembly="true" />
```

> The project reference could also be a Nuget package reference.

The generator will be automatically executed when the project is built, and the
generated code will be added to your project.

## See also

- [Microsoft Dependency Injection][ms-dependency-injection]

[ms-dependency-injection]: https://docs.microsoft.com/en-us/aspnet/core/fundamentals/dependency-injection?view=aspnetcore-3.1
[ActivatorUtilitiesConstructor]: https://docs.microsoft.com/en-us/dotnet/api/microsoft.extensions.dependencyinjection.activatorutilitiesconstructorattribute?view=dotnet-plat-ext-3.1
[hosting-extensions]: https://learn.microsoft.com/en-us/dotnet/core/extensions/generic-host
