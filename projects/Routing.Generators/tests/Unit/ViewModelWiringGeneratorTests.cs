// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Generators;

using System.Diagnostics.CodeAnalysis;

/// <summary>
/// Snapshot tests for the <see cref="ViewModelWiringGenerator" />.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
public class ViewModelWiringGeneratorTests : VerifyBase
{
    /// <summary>
    /// Verifies that when a View class is properly annotated with a
    /// `ViewModelAttribute`
    /// using a valid ViewModel type, the generator will correctly generate the
    /// extensions for the View class.
    /// </summary>
    /// <returns>The asynchronous task for this test case.</returns>
    [TestMethod]
    public Task GenerateViewExtensionsCorrectly()
    {
        const string source = """
                              namespace DroidNet.Routing.Generators;

                              using DroidNet.Routing.Generators;

                              public class TestViewModel;

                              [ViewModel(typeof(TestViewModel))]
                              public sealed partial class TestView
                              {
                              }
                              """;

        // Use verify to snapshot test the source generator output!
        var driver = TestHelper.GeneratorDriver(source);
        var runResults = driver.GetRunResult();
        return this.Verify(runResults).UseDirectory("Snapshots");
    }

    /// <summary>
    /// Verifies that the generator will issue a specific diagnostic if the
    /// attribute is malformed.
    /// </summary>
    /// <returns>The asynchronous task for this test case.</returns>
    [TestMethod]
    public Task IssueDiagnosticWhenMalformedAttribute()
    {
        const string source = """
                              namespace DroidNet.Routing.Generators;

                              using DroidNet.Routing.Generators;

                              public class TestViewModel;

                              [ViewModel(12)]
                              public sealed partial class TestView
                              {
                              }
                              """;

        // Use verify to snapshot test the source generator output!
        var driver = TestHelper.GeneratorDriver(source);
        var runResults = driver.GetRunResult();
        return this.Verify(runResults).UseDirectory("Snapshots");
    }

    /// <summary>
    /// Verifies that the generator will issue a specific diagnostic if the
    /// ViewModel type in the attribute cannot be found.
    /// </summary>
    /// <returns>The asynchronous task for this test case.</returns>
    [TestMethod]
    public Task IssueDiagnosticWhenViewModelNotFound()
    {
        const string source = """
                              namespace DroidNet.Routing.Generators;

                              using DroidNet.Routing.Generators;

                              public class TestViewModel;

                              [ViewModel(typeof(ViewModel2)]
                              public sealed partial class TestView
                              {
                              }
                              """;

        // Use verify to snapshot test the source generator output!
        var driver = TestHelper.GeneratorDriver(source);
        var runResults = driver.GetRunResult();
        return this.Verify(runResults).UseDirectory("Snapshots");
    }
}
