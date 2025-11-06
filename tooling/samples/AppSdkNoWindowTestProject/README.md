# Windows App SDK No-Window Test Project Template

This template demonstrates how to create unit test projects for Windows App SDK
applications when you don't need to test UI controls or interact with windows.
It provides a minimal setup that still allows running tests on the UI thread
when needed.

## Core Files

- `App.xaml` / `App.xaml.cs` - Application entry point that:
  - Initializes the test platform
  - Sets up the UI dispatcher
  - Configures test execution without creating windows

- `Package.appxmanifest` - MSIX package manifest containing:
  - App identity and capabilities
  - Required dependencies
  - Minimum Windows version requirements

## Test Project Organization

```text
/
├── App.xaml                 # WinUI application definition
├── App.xaml.cs             # Test platform initialization
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
- Configures Windows App SDK without creating visible windows
- Supports running tests on the UI thread using UITestMethodAttribute
- Minimal dependencies and setup required

## Writing Tests

Here's an example of how to write a test that needs to run on the UI thread:

```csharp
[TestClass]
public class MyTests
{
    [UITestMethod]
    public void UIThreadTest()
    {
        // This test runs on the UI thread
        var dispatcher = DispatcherQueue.GetForCurrentThread();
        Assert.IsNotNull(dispatcher);
    }

    [TestMethod]
    public void NormalTest()
    {
        // This test runs on a normal test thread
        Assert.IsTrue(true);
    }
}
```

The `UITestMethod` attribute ensures the test runs on the UI thread, which is required when testing certain Windows App SDK APIs that have thread affinity.

## Using This Template

To create a new test project from this template:

1. **Copy Project Files**
   - Copy all files to a new directory
   - Rename the solution/project files:
     - `AppSdkNoWindowTestProject.csproj` → `YourProject.csproj`

2. **Update Project Configuration**
   - In `YourProject.csproj`:
     - Change `<RootNamespace>DroidNet.Samples.Tests</RootNamespace>`

3. **Modify App Identity**
   - In `Package.appxmanifest`:
     - Update `<Identity Name="...">`
     - Change `<DisplayName>`
     - Update `<Description>`
     - Modify `<Application Id="AppSdkWinUITestProject"`

4. **Update Source Files**
   - In `App.xaml`:
     - Change `x:Class="DroidNet.Samples.Tests.App"`
     - Update namespace declarations
   - In `App.xaml.cs`:
     - Update namespace to match your project
   - In test files:
     - Update namespace declarations
     - Rename test classes as needed

5. **Visual Studio Integration**
   - Generate the solution file with `SlnGen` in your `open.cmd` script
   - Clean and rebuild solution
   - Verify test discovery works
   - Run sample tests to confirm setup
