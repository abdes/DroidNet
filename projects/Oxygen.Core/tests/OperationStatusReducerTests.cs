// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Core.Diagnostics;

namespace DroidNet.TestHelpers.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Operation Results")]
public class OperationStatusReducerTests
{
    private readonly OperationStatusReducer reducer = new();

    [TestMethod]
    public void Reduce_ShouldReturnCancelled_WhenOperationWasCancelled()
    {
        var diagnostics = new[]
        {
            MakeDiagnostic(DiagnosticSeverity.Error),
        };

        var result = this.reducer.Reduce(primaryGoalCompleted: false, wasCancelled: true, diagnostics);

        _ = result.Should().Be(OperationStatus.Cancelled);
    }

    [TestMethod]
    public void Reduce_ShouldReturnFailed_WhenPrimaryGoalDidNotComplete()
    {
        var result = this.reducer.Reduce(primaryGoalCompleted: false, wasCancelled: false, []);

        _ = result.Should().Be(OperationStatus.Failed);
    }

    [TestMethod]
    [DataRow(DiagnosticSeverity.Info, OperationStatus.Succeeded)]
    [DataRow(DiagnosticSeverity.Warning, OperationStatus.SucceededWithWarnings)]
    [DataRow(DiagnosticSeverity.Error, OperationStatus.PartiallySucceeded)]
    [DataRow(DiagnosticSeverity.Fatal, OperationStatus.PartiallySucceeded)]
    public void Reduce_ShouldUseMaximumDiagnosticSeverity_WhenPrimaryGoalCompleted(
        DiagnosticSeverity severity,
        OperationStatus expectedStatus)
    {
        var diagnostics = new[]
        {
            MakeDiagnostic(severity),
        };

        var result = this.reducer.Reduce(primaryGoalCompleted: true, wasCancelled: false, diagnostics);

        _ = result.Should().Be(expectedStatus);
    }

    [TestMethod]
    public void ComputeSeverity_ShouldReturnMaximumSeverity()
    {
        var diagnostics = new[]
        {
            MakeDiagnostic(DiagnosticSeverity.Warning),
            MakeDiagnostic(DiagnosticSeverity.Error),
            MakeDiagnostic(DiagnosticSeverity.Info),
        };

        var result = this.reducer.ComputeSeverity(diagnostics);

        _ = result.Should().Be(DiagnosticSeverity.Error);
    }

    private static DiagnosticRecord MakeDiagnostic(DiagnosticSeverity severity)
        => new()
        {
            OperationId = Guid.NewGuid(),
            Domain = FailureDomain.ProjectValidation,
            Severity = severity,
            Code = DiagnosticCodes.ProjectPrefix + "TEST",
            Message = "Test diagnostic",
        };
}
