# Generating solution files

Visual Studio solutions generally do not scale well for large project trees.
They are scoped views of a set of projects. Maintaining Visual Studio solutions
becomes hard because you have to keep them in sync with the other build logic.
Therefore, we do not use pre-made solution files. Instead we use [Visual Studio
solution generator](https://microsoft.github.io/slngen/).

SlnGen reads the project references of a given project to create a Visual Studio
solution on demand. For example, you can run it against a unit test project and
be presented with a Visual Studio solution containing the unit test project and
all of its project references. You can also run SlnGen against a traversal
project in a rooted folder to open a Visual Studio solution containing that view
of your project tree.

See detailed instructions for how to get started and use SlnGen on its [project
web site](https://microsoft.github.io/slngen/).

When working on a certain project, you can generate a solution for that project
and all its references by running the following command:

```shell
MSBuild /Target:SlnGen /Verbosity:Minimal /NoLogo
```

To generate without launching visual studio:

```shell
MSBuild /Target:SlnGen /Verbosity:Minimal /NoLogo /Property:"SlnGenLaunchVisualStudio=false"
```

## Helper scripts

For the purpose of CI, we need to build all projects. The
[`GenerateAllSolution.ps1`](../GenerateAllSolution.ps1) script can do that.
