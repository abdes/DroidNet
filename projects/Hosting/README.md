# Generic Host for WinUI applications

The [.Net Hosting
extensions](https://learn.microsoft.com/en-us/dotnet/core/extensions/generic-host)
look like they are only for ASP .Net but they are not. The documentation now
introduces them as part of the [.Net
fundamentals](https://learn.microsoft.com/en-us/dotnet/core/extensions/generic-host)
and they can very well be used in any scenario including console and desktop
applications.

A host is an object that encapsulates an app's resources and lifetime
functionality, such as:

- Dependency injection (DI)
- Configuration (in-memory, json, secrets, environment variables, command line)
- Logging
- Diagnostics
- Different types of File Providers
- App startup/shutdown and IHostedService implementations

➡ What if we could leverage all of that in a WinUI application?

You really don't need a dozen of Nugets from many maintained or no longer
maintained sources. All of that can be done with just the .Net framework. Let's
see how.

## Before we start

The starting point for the application will be the basic WinUI application with
a custom `Main` entry point. Not that it is absolutely required, but it will
give us full control of the setup of the Generic Host environment and will make
the WinUI subsystem just another service for which we manage the lifecycle.

See <https://github.com/abdes/winui-override-main> for the basics of how to
override the default entry point.

## A hosted service for WinUI

A typical WinUI application runs in a UI thread, assumed to be the main thread,
and for which the dispatched is initialized in the `Main` entry point.

What we want is to have that thread be a background service, hosted by the
generic host, along with the other application services. This will give us the
advantage of managing the UI just as another service, and of course getting all
the goodies of the Generic Host listed above.

The implementation, extensively documented in the source code, adds a
[UserInterfaceHostedService](Hosting/Desktop/WinUI/UserInterfaceHostedService.cs)
which runs the ['UI Thread'](Hosting/Desktop/WinUI/UserInterfaceThread.cs)
as a background service. We can decide, based on the options in the
[HostingContext](Hosting/Desktop/WinUI/HostingContext.cs) if we want the
lifetime of that 'UI Thread' to be linked to the application lifetime or not. In
other words, we can decide of termination of the 'UI Thread' results in
termination of the application or not, and vice versa.

## An extension method for the builder

To simplify the setup of the User Interface hosted service, an [extension
method](Hosting/Desktop/WinUI/HostingExtensions.cs) to the host builder is
provided.

This greatly simplifies the host building as it is now just a matter of calling
that extension method.

```csharp
    private static void Main(string[] args)
    {
        // Use a default application host builder, which comes with logging,
        // configuration providers for environment variables, command line,
        // appsettings.json and secrets.
        var builder = Host.CreateApplicationBuilder(args);

        // You can further customize and enhance the builder with additional
        // configuration sources, logging providers, etc.

        // Setup and provision the hosting context for the User Interface
        // service.
        ((IHostApplicationBuilder)builder).Properties.Add(
            key: HostingExtensions.HostingContextKey,
            value: new HostingContext() { IsLifetimeLinked = true });

        // Add the WinUI User Interface hosted service as early as possible to
        // allow the UI to start showing up while you continue setting up other
        // services not required for the UI.
        var host = builder.ConfigureWinUI<App, MainWindow>().Build();

        // Finally start the host. This will block until the application
        // lifetime is terminated through CTRL+C, closing the UI windows or
        // programmatically.
        host.Run();
    }
```

## Why the UI hosted service does not create the MainWindow?

Simply because not all applications are single window applications and it is not
obvious in a multi-window application which window will terminate the UI when
closed. Therefore, it is much more preferable to continue having the
`Application` responsible for managing its `Window`s.

## Can this be adapted to WPF, etc. ?

Yes. Simply implement the classes inside WinUI namespace for any other UI
framework.
