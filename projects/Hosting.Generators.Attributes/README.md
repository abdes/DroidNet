# Attributes for Hosting Source Generators

This is a .NET Standard 2.0 library that provides an attribute for marking a
class or interface for automatic dependency injection registration.

It's important that these attributes are in a separate project, because their
usage in both, the generator and the consumer projects, is problematic. The
generator, in particular needs to reference and pack the attributes assembly in
a very specific way. This is documented extensively in the
[generator project file](../Hosting.Generators/src/Hosting.Generators.csproj).

## InjectAsAttribute

The InjectAsAttribute is used to mark a class or interface for automatic
dependency injection registration.

### Properties

- `Lifetime`: Gets the specified ServiceLifetime of the service. ServiceLifetime
  is an enumeration with values Singleton, Scoped, and Transient, which describe
  how the lifetime of the service is managed.

- `Key`: Gets or sets the key for the service, if any. This can be used to
  differentiate between multiple registrations of the same service type.

- `ImplementationType`: Gets or sets the implementation type, if the target is
  an interface. This specifies the concrete type that should be used when an
  instance of the interface is requested.

### Example Usage

To use `InjectAsAttribute`, simply apply it to a class or interface and specify
the ServiceLifetime. Optionally, you can also specify a Key and
ImplementationType.

```csharp
[InjectAs(ServiceLifetime.Singleton)]
public class MySingletonService
{
    // Implementation goes here
}
```

In the above example, MySingletonService will be registered as a singleton
service in the dependency injection container.
