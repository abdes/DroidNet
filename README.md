# README

<!-- markdownlint-disable-next-line no-inline-html -->
<div align="center">

[![Windows][windows-badge]][WinUI]
[![pre-commit][pre-commit-badge]][pre-commit]

</div>

> A toolkit and example for mono-repo style WindowsAppSdk development with
> Visual Studio.

[windows-badge]: https://img.shields.io/badge/OS-windows-blue
[WinUI]: https://learn.microsoft.com/en-us/windows/apps/winui/
[pre-commit-badge]: https://img.shields.io/badge/pre--commit-enabled-brightgreen?logo=pre-commit
[pre-commit]: https://github.com/pre-commit/pre-commit

## Overview

“DroidNet” is envisioned as a comprehensive suite of sub-projects aimed at
automating and streamlining the development, testing, and continuous integration
of WinUI apps. “DroidNet” is designed to be a robust and comprehensive solution
for developing WinUI applications. Its focus on automation and quality
assurance, combined with its modular architecture, makes it a powerful tool for
any developer working with WinUI and WinAppSDK. Happy coding!

## Features

"DroidNet" has been designed with a focus on the following features:

- **Automation**: Automate repetitive tasks in your development process, such as
  building, testing, and deployment.

- **Modularity**: The project has been structured as a mono-repo, allowing for
  easy management and separation of concerns between different sub-projects.

- **Quality Assurance**: Includes built-in tools for code linting and
  formatting, as well as pre-commit hooks to ensure code quality before commits.

- **Integration with WinUI and WinAppSDK**: The project is designed to work
  seamlessly with WinUI and WinAppSDK, allowing developers to leverage the
  latest technologies in Windows app development.

### Docking framework for WinUI 3

The [Docking](projects/Docking/) project contains a flexible docking frameowrk
for WinUI. Dockable views can be embedded in Docks, which are managed in a tree
structure by a Docker. Combined with a pluggable layout engine, the docking tree
can be rendered into dock panels which can be attached to the workspace edges or
relative to other docks.

![Example docking workspace](media/routing-debugger.png "Docking Workspace")

### MVVM ViewModel first router

Similar to what Angular does in a web application, the [Router](projects/Routing/)
provides a routing frameowrk to navigate within the WinUI application using URIs.
With the provided source generators, it is easy to declare views, view models, and
wire them together. The routing configuration is completely declarative and follows
the same principles than Angular.

### Application host and Dependency Container

The [Hosting](projects/Hosting/) project offers an integration with .Net Host and
the DryIoc container. The source generators automate the injection of services and
view models and a view locator service makes it intuitive to locate a view for a
particular view model.

## Getting Started

To get started with "DroidNet", you'll need to have Visual Studio installed on
your machine. You can then clone the repository and open the solution in Visual
Studio to start developing. Simply use the `open.cmd` script in any of the folders
to generate the solution file and open it in Visual Studio.

## Contributions

Contributions to "DroidNet" are welcome! If you have a feature request, bug
report, or want to contribute to the code, please feel free to open an issue or
a pull request.

## License

"DroidNet" is licensed under the MIT License. See the LICENSE file for more
information.
