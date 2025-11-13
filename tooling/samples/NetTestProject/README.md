# .Net Test Project Template

NetTestProject is a sample unit testing project for a .NET class library. This project demonstrates how to set up and use unit tests in a .NET environment using MSTest and AwesomeAssertions.

## Project Setup

The project is set up using a .NET class library with a corresponding unit test project. The main components of the setup include:

- **NetTestProject.Tests.csproj**: The project file for the unit test project.
- **SampleTest.cs**: A sample test class containing unit tests.

### Project File

The `NetTestProject.Tests.csproj` file is designed to focus on the project specific dependencies and configurations for running the unit tests. All references and configuration of MSTest and related dependencies, including AwesomeAssertions, are already taken care of in the common MSBuild props files.

## Adding Test Cases

Test cases are added using `MSTest` and `AwesomeAssertions`. If mocking is required, `Moq` would be the framework of choice. Here is an example of a test class with test methods:

```csharp
namespace DroidNet.Samples.Tests;
using AwesomeAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;

[TestClass]
public class SampleTest
{
    [TestMethod]
    [DataRow(1, 1, 2)]
    [DataRow(5, 24, 29)]
    public void AddTwoNumbers_Works(int first, int second, int expected)
    {
        // Act
        var sum = first + second;

        // Assert
        _ = sum.Should().Be(expected);
    }
}
```

### Writing Tests

1. **Test Class**: Create a test class and annotate it with `[TestClass]`.
2. **Test Methods**: Add test methods and annotate them with `[TestMethod]`.
3. **Data-Driven Tests**: Use `[DataRow]` to provide multiple sets of data to the test method.
4. **Assertions**: Use AwesomeAssertions to assert the expected outcomes.

## Running Tests

To run the tests, use the test explorer in Visual Studio or run the following command in the terminal:

```sh
dotnet test
```

This command will build the project and execute all the tests, providing a summary of the test results.
