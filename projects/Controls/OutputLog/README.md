# DroidNet.Controls.OutputLog

![DroidNet Logo](https://i.imgur.com/Cl8EZJL.png)

The `OutputLog` control is a custom control, providing a reusable and customizable logging view for displaying log events in desktop applications using WinUI 3. This control integrates with Serilog for logging functionality and allows you to output logs to a `RichTextBlock` control with theming support.

## Features

- Displays log events formatted according to the specified output template.
- Supports rich text formatting using a `RichTextBlock` control, allowing colored text, links, and other enhancements.
- Integrates seamlessly with Serilog for logging functionality.
- Provides theming support through the [DroidNet.Controls.OutputLog.Theming](https://droidnet.github.io/dotnet/api/DroidNet.Controls.OutputLog.Theming/) namespace, enabling you to apply predefined or custom themes to the output log.

## Usage


1. First, install the `DroidNet.Controls` package via dotnet CLI, nuget, etc:
    ```
    dotnet add package DroidNet.Controls.OutputLog
    ```

2. Import the required namespaces in your XAML file:
    ```xml
    <Page
        ...
        xmlns:dnc="using:DroidNet.Controls"
        ...>
    </Page>
    ```

3. Add an `OutputLogView` control to your window or page, and set its `OutputLogSink` property with an instance of `DelegatingSink<RichTextBlockSink>`:

   ```xaml
   <droidnet:OutputLogView x:Name="outputLogView" />
   ```

4. In your code-behind or view model, configure Serilog to output logs to the `OutputLogView` control using the `LoggerSinkConfigurationOutputLogExtensions.OutputLogView<T>` method:

TODO: provide detailed usage example for OutputLog

## Samples & Examples

Explore the [DroidNet Controls demo project](../DemoApp/README.md) for a working example demonstrating various features and use cases of the OutputLog control.

*Happy coding! 🚀*
