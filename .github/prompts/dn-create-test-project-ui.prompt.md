---
mode: 'agent'
tools: ['codebase', 'editFiles', 'search', 'terminalLastCommand', 'vscodeAPI']
description: 'Create a UI test project following Droidnet patterns and best practices'
---

# DroidNet UI Test Project

Your goal is to scaffold a new test project for UI testing, following the best practices from similar project in the DroidNet codebase.

## Required Tools

This task requires the following tools to be enabled:
- `codebase` - To search and analyze existing UI test projects
- `editFiles` - **REQUIRED** - To create and modify test project files
- `search` - To find similar test projects and patterns
- `terminalLastCommand` - **OPTIONAL** - To run commands for copying assets
- `vscodeAPI` - **OPTIONAL** - For VS Code integration

**Important**: The `editFiles` tool should be used to create new files by writing content to non-existent file paths. If this tool is not available, the agent should provide PowerShell/shell commands for manual execution.

## Project Setup

### Directory Structure
Create a test project with the following structure:
```
[ProjectName].UI.Tests/
├── App.xaml
├── App.xaml.cs
├── Package.appxmanifest
├── [ProjectName].UI.Tests.csproj
├── [ClassUnderTest]Tests.cs
├── Assets/
│   ├── LockScreenLogo.scale-200.png
│   ├── SplashScreen.scale-200.png
│   ├── Square150x150Logo.scale-200.png
│   ├── Square44x44Logo.scale-200.png
│   ├── Square44x44Logo.targetsize-24_altform-unplated.png
│   ├── StoreLogo.png
│   └── Wide310x150Logo.scale-200.png
└── Properties/
    ├── app.manifest
    ├── AssemblyInfo.cs
    ├── GlobalSuppressions.cs
    ├── launchSettings.json
    └── PublishProfiles/
        └── win-x64.pubxml
```

### Required Files

#### 1. Project File (`.csproj`)
- Use naming convention `[ProjectName].UI.Tests.csproj`
- Set `RootNamespace` to `$(RootNamespace).[ProjectName].Tests`
- Set `AssemblyName` to `DroidNet.[ProjectName].UI.Tests`
- Set `AllowUnsafeBlocks` to `true`
- Reference the project under test
- Import `UITests.Shared.projitems`
- Include `Moq` package reference

#### 2. App Files (`App.xaml` and `App.xaml.cs`)
- Inherit from `VisualUserInterfaceTestsApp` in both files
- Use proper namespace: `DroidNet.[ProjectName].Tests`
- Merge necessary resource dictionaries (XamlControlsResources at minimum)
- Include project-specific resources if needed

#### 3. Package Manifest (`Package.appxmanifest`)
- Generate unique GUID for `Identity.Name`
- Set appropriate `DisplayName` and `Description`
- Include both Windows.Universal and Windows.Desktop target families
- Configure `runFullTrust` capability
- Set proper visual elements (logo paths, colors, etc.)

#### 4. Properties Files

##### `Properties/app.manifest`
- Configure DPI awareness: `PerMonitorV2, PerMonitor`
- Include Windows 8+ compatibility section
- Standard XML manifest structure

##### `Properties/AssemblyInfo.cs`
- Add `[assembly: DoNotParallelize]` attribute
- Include MIT license header

##### `Properties/GlobalSuppressions.cs`
- Suppress CA5392 (DefaultDllImportSearchPaths for P/Invokes)
- Suppress IDE0130 (Namespace does not match folder structure)
- Suppress CA1707 (Identifiers should not contain underscores) for test namespace
- Suppress SA1600/SA1601 (XMLDoc comments) for test namespace
- Suppress CA1515 (Consider making public types internal) for test namespace
- Update namespace in Target attribute to match your project

##### `Properties/launchSettings.json`
- Define two profiles: "Unit Tests (Package)" and "Unit Tests (Unpackaged)"
- Package profile uses `MsixPackage` commandName
- Unpackaged profile uses `Project` commandName

##### `Properties/PublishProfiles/win-x64.pubxml`
- Configure FileSystem publish protocol
- Set Platform to x64 and RuntimeIdentifier to win-x64
- Enable SelfContained deployment
- Configure PublishReadyToRun based on configuration

#### 5. Assets Folder
Copy all standard WinUI test app assets from an existing test project (7 image files)

### Test Classes
- Create test classes that match the classes being tested (e.g., `CalculatorTests` for `Calculator`)
- Remember that tests that require UI are asynchronous
- Use `[ExcludeFromCodeCoverage]` attribute on test classes

## Test Structure

- Use `[TestClass]` attribute for test classes
- Use `[TestMethod]` attribute for test methods
- Follow the Arrange-Act-Assert (AAA) pattern
- Name tests using the pattern `MethodName_Scenario_ExpectedBehavior`
- Use `[TestInitialize]` and `[TestCleanup]` for per-test setup and teardown
- Use `[ClassInitialize]` and `[ClassCleanup]` for per-class setup and teardown
- Use `[AssemblyInitialize]` and `[AssemblyCleanup]` for assembly-level setup and teardown

## Standard Tests

- Keep tests focused on a single behavior
- Avoid testing multiple behaviors in one test method
- Use clear assertions that express intent
- Include only the assertions needed to verify the test case
- Make tests independent and idempotent (can run in any order)
- Avoid test interdependencies

## Data-Driven Tests

- Use `[TestMethod]` combined with data source attributes
- Use `[DataRow]` for inline test data
- Use `[DynamicData]` for programmatically generated test data
- Use `[TestProperty]` to add metadata to tests
- Use meaningful parameter names in data-driven tests

## Assertions

- Use AwesomeAssertions to assert
- Ensure assertions are simple in nature and have a message provided for clarity on failure

## Mocking and Isolation

- Consider using Moq alongside MSTest
- Mock dependencies to isolate units under test
- Use interfaces to facilitate mocking
- Consider using a DI container for complex test setups. I use DryIoc as the DI provider.

## Test Organization

- Group tests by feature or component
- Use test categories with `[TestCategory("Category")]`
- Use test priorities with `[Priority(1)]` for critical tests

## File Templates

### Properties/app.manifest
```xml
<?xml version="1.0" encoding="utf-8"?>
<assembly manifestVersion="1.0" xmlns="urn:schemas-microsoft-com:asm.v1">
    <assemblyIdentity version="1.0.0.0" name="UnitTest.app"/>
    <compatibility xmlns="urn:schemas-microsoft-com:compatibility.v1">
        <application>
            <supportedOS Id="{4a2f28e3-53b9-4441-ba9c-d69d4a4a6e38}" />
        </application>
    </compatibility>
    <application xmlns="urn:schemas-microsoft-com:asm.v3">
        <windowsSettings>
            <dpiAware xmlns="http://schemas.microsoft.com/SMI/2005/WindowsSettings">true/PM</dpiAware>
            <dpiAwareness xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">PerMonitorV2, PerMonitor</dpiAwareness>
        </windowsSettings>
    </application>
</assembly>
```

### Properties/AssemblyInfo.cs
```csharp
// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

[assembly: DoNotParallelize]
```

### Properties/GlobalSuppressions.cs
```csharp
// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

[assembly: SuppressMessage("Security", "CA5392:Use DefaultDllImportSearchPaths attribute for P/Invokes", Justification = "external code, not under our control", Scope = "member", Target = "~M:Microsoft.Windows.Foundation.UndockedRegFreeWinRTCS.NativeMethods.WindowsAppRuntime_EnsureIsLoaded~System.Int32")]
[assembly: SuppressMessage("Style", "IDE0130:Namespace does not match folder structure", Justification = "all controls are under the namespace DroidNet.Controls", Scope = "namespace", Target = "~N:DroidNet.[ProjectName].Tests")]
[assembly: SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Test method names are more readable with underscores", Scope = "namespaceanddescendants", Target = "~N:DroidNet.[ProjectName].Tests")]
[assembly: SuppressMessage("StyleCop.CSharp.DocumentationRules", "SA1600:Elements should be documented", Justification = "Test cases do not require XMLDoc comments", Scope = "namespaceanddescendants", Target = "~N:DroidNet.[ProjectName].Tests")]
[assembly: SuppressMessage("StyleCop.CSharp.DocumentationRules", "SA1601:Partial elements should be documented", Justification = "Test cases do not require XMLDoc comments", Scope = "namespaceanddescendants", Target = "~N:DroidNet.[ProjectName].Tests")]
[assembly: SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "test classes need to be public", Scope = "namespaceanddescendants", Target = "~N:DroidNet.[ProjectName].Tests")]
```
**Note**: Replace `[ProjectName]` with the actual project name in the Target attributes.

### Properties/launchSettings.json
```json
{
    "profiles": {
        "Unit Tests (Package)": {
            "commandName": "MsixPackage"
        },
        "Unit Tests (Unpackaged)": {
            "commandName": "Project"
        }
    }
}
```

### Properties/PublishProfiles/win-x64.pubxml
```xml
<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
    <PropertyGroup>
        <PublishProtocol>FileSystem</PublishProtocol>
        <Platform>x64</Platform>
        <RuntimeIdentifier>win-x64</RuntimeIdentifier>
        <PublishDir>bin\$(Configuration)\$(TargetFramework)\$(RuntimeIdentifier)\publish\</PublishDir>
        <SelfContained>true</SelfContained>
        <PublishSingleFile>False</PublishSingleFile>
        <PublishReadyToRun Condition="'$(Configuration)' == 'Debug'">False</PublishReadyToRun>
        <PublishReadyToRun Condition="'$(Configuration)' != 'Debug'">True</PublishReadyToRun>
        <PublishAot>False</PublishAot>
    </PropertyGroup>
</Project>
```

## Example UI Test

```csharp
    [TestMethod]
    public Task DismissWhileOpenClosesPopup_Async() => EnqueueAsync(async () =>
    {
        var context = await PopupMenuHostTestContext.CreateAsync(1).ConfigureAwait(true);

        try
        {
            var openedTcs = CreateSignal();
            var closedTcs = CreateSignal();
            context.Host.Opened += (_, _) => openedTcs.TrySetResult(true);
            context.Host.Closed += (_, _) => closedTcs.TrySetResult(true);

            _ = context.Host.ShowAt(context.Anchors[0], MenuNavigationMode.PointerInput);

            await WaitForEventAsync(openedTcs.Task, "Popup should open before dismissal").ConfigureAwait(true);
            _ = context.Host.IsOpen.Should().BeTrue();

            context.Host.Dismiss(MenuDismissKind.PointerInput);

            await WaitForEventAsync(closedTcs.Task, "Popup should close after dismissal").ConfigureAwait(true);
            _ = context.Host.IsOpen.Should().BeFalse();
        }
        finally
        {
            context.Dispose();
        }
    });
```

## Example UI Test using the VisualStateManager wrapper

```csharp
    [TestMethod]
    public Task ShowsAcceleratorTextWhenSet_Async() => EnqueueAsync(async () =>
    {
        // Arrange - Start without accelerator text
        var (menuItem, vsm) = await SetupMenuItemWithData(new MenuItemData
        {
            Text = "Save",
            AcceleratorText = null,
        }).ConfigureAwait(true);

        // Act - Set accelerator text to trigger state change
        menuItem.ItemData!.AcceleratorText = "Ctrl+S";
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = vsm.GetCurrentStates(menuItem).Should().Contain([MenuItem.HasAcceleratorVisualState]);
        CheckPartIsThere(menuItem, MenuItem.AcceleratorTextBlockPart);
    });
```
