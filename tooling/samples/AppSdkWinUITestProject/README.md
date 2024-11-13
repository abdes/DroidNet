# Windows App SDK Windowed Test Project Template

This template demonstrates how to create unit test projects for Windows App SDK applications that need to test UI controls and interact with windows. It provides a minimal but complete setup for testing Windows App SDK functionality that requires a window context.

# Project Structure

The solution follows a standard Windows App SDK project layout optimized for UI testing:

## Core Files

- `App.xaml` / `App.xaml.cs`
  - Defines the application entry point.
  - Initializes the test platform.
  - Configures the main window and sets up the UI dispatcher for tests.

- `MainWindow.xaml` / `MainWindow.xaml.cs`
  - Defines the main test window.
  - Provides a window context for UI tests.
  - Has a fixed size of 800x600 and can host UI controls under test.

- `Package.appxmanifest`
  - Contains the MSIX package manifest.
  - Defines app identity, capabilities, required dependencies, and minimum Windows version requirements.

## Test Project Organization
```
/
├── App.xaml                 # WinUI application definition
├── App.xaml.cs             # Test platform initialization
├── MainWindow.xaml         # Test window definition
├── MainWindow.xaml.cs      # Test window code
├── Package.appxmanifest    # Package configuration
├── Assets/                 # Required app icons & images
│   ├── Square150x150Logo.png
│   ├── Square44x44Logo.png
│   └── Wide310x150Logo.png
└── Tests/                  # Test classes go here
    └── *.Tests.cs         # Individual test files
```

## Key Facts
- Uses MSTest v3.6+ test framework
- Targets Windows 10.0.17763.0+
- References Windows App SDK 1.6+
- Configured for x64 platform
- Creates a main window for UI testing context
- Supports running tests on the UI thread using UITestMethodAttribute
- Minimal dependencies and setup required

## Writing Tests

Here's an example of how to write a test that interacts with UI controls:

```csharp
[TestClass]
public class MyTests
{
    [UITestMethod]
    public void TestUIControl()
    {
        // This test runs on the UI thread
        var grid = new Grid();
        _ = grid.Should().NotBeNull();

        // Add controls to test
        var button = new Button();
        grid.Children.Add(button);

        // Test UI interactions
        button.IsEnabled = false;
        Assert.IsFalse(button.IsEnabled);
    }
}
```

The `UITestMethod` attribute ensures the test runs on the UI thread, which is required when testing Windows App SDK UI controls.

## Using This Template

To create a new test project from this template:

1. **Copy Project Files**
   - Copy all files to a new directory
   - Rename the solution/project files:
     - WithWindow.UI.Tests.csproj → `YourProject.csproj`

2. **Update Project Configuration**
   - In `YourProject.csproj`:
     - Change `<RootNamespace>DroidNet.Samples.Tests</RootNamespace>`

3. **Modify App Identity**
   - In `Package.appxmanifest`:
     - Update `<Identity Name="...">`
     - Change `<DisplayName>`
     - Update `<Description>`

4. **Update Source Files**
   - In `xaml` files:
     - Change `x:Class` namespace
   - In All source code files:
     - Update namespace to match your project
   - In test files:
     - Rename test classes as needed

5. **Visual Studio Integration**
   - Generate the solution file with `SlnGen` in your `open.cmd` script
   - Clean and rebuild solution
   - Verify test discovery works
   - Run sample tests to confirm setup
