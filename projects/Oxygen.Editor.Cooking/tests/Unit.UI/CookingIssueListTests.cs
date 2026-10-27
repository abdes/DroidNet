// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using AwesomeAssertions;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Cooking.Unit.UI.Tests;

[TestClass]
internal sealed class CookingIssueListTests
{
    /// <summary>Diagnostics that say the same thing share one issue that keeps every place; errors come first.</summary>
    [TestMethod]
    public void RepeatedDiagnosticsShareOneIssueWithTheirPlaces()
    {
        var issues = new CookingIssueList();
        var repeated = Enumerable.Range(0, 3).Select(static index => Diagnostic("Textures repeat.", "Cook.Texture", DiagnosticSeverity.Warning, string.Create(CultureInfo.InvariantCulture, $"/materials/{index}"))).ToArray();

        foreach (var diagnostic in repeated)
        {
            issues.Report(diagnostic, "Crate");
        }

        issues.Report(repeated[0], "Crate");
        issues.Report(Diagnostic("Emissive strength is left out.", "Cook.Emissive", DiagnosticSeverity.Warning, "/materials/1"), "Crate");
        issues.Report(Diagnostic("A geometry could not be loaded.", "Cook.Geometry", DiagnosticSeverity.Error, "/meshes/0"), "Crate");
        issues.Reorder();

        _ = issues.Should().HaveCount(3);
        _ = issues.Total.Should().Be(5);
        _ = issues[0].Message.Should().Be("A geometry could not be loaded.");
        _ = issues[1].Count.Should().Be(3);
        _ = issues[1].Places.Select(static place => place.Path).Should().Equal("/materials/0", "/materials/1", "/materials/2");
        _ = issues[1].Places.Should().OnlyContain(place => place.Kind == "Material");
        _ = issues[1].Caption.Should().Be("Crate · Material");
        _ = issues[1].CountText.Should().Be("×3");
        _ = issues[1].CopyText.Should().Be(string.Join(Environment.NewLine, "Textures repeat.", "Places: 3", "Scope: Crate", string.Empty, "/materials/0 · Material", "/materials/1 · Material", "/materials/2 · Material"));
        _ = issues[1].CountVisibility.Should().Be(Microsoft.UI.Xaml.Visibility.Visible);
        _ = issues[0].CountVisibility.Should().Be(Microsoft.UI.Xaml.Visibility.Collapsed);
    }

    private static DiagnosticRecord Diagnostic(string message, string code, DiagnosticSeverity severity, string location) => new()
    {
        OperationId = Guid.NewGuid(),
        Domain = FailureDomain.ContentPipeline,
        Severity = severity,
        Code = code,
        Message = message,
        TechnicalMessage = location,
    };
}
