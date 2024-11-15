// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Mvvm.Generators.Tests;

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;

/// <summary>
/// Snapshot tests for the <see cref="ViewModelWiringGenerator" />.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("ViewModel Wiring Generator")]
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
                              namespace Testing;

                              using DroidNet.Mvvm.Generators;

                              [ViewModel(typeof(TestViewModel))]
                              public partial class TestView
                              {
                              }

                              public class TestViewModel;
                              """;

        // Use verify to snapshot test the source generator output!
        var driver = TestHelper.GeneratorDriver(source);
        var runResults = driver.GetRunResult();
        return this.Verify(runResults.Results.FirstOrDefault().GeneratedSources.FirstOrDefault().SourceText.ToString())
            .UseDirectory("Snapshots");
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
                              namespace Testing;

                              using DroidNet.Mvvm.Generators;

                              public class TestViewModel;

                              [ViewModel(12)]
                              [ExcludeFromCodeCoverage]
                              public sealed partial class TestView
                              {
                              }
                              """;

        // Use verify to snapshot test the source generator output!
        var driver = TestHelper.GeneratorDriver(source);
        var runResults = driver.GetRunResult();
        runResults.Results.Length.Should().Be(1);
        runResults.Results[0].Diagnostics.Length.Should().Be(1);
        return this.Verify(runResults.Results[0].Diagnostics[0].ToString()).UseDirectory("Snapshots");
    }
}
