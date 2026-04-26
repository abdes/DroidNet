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
public class ExceptionDiagnosticAdapterTests
{
    private readonly ExceptionDiagnosticAdapter adapter = new();

    [TestMethod]
    public void ToDiagnostic_ShouldMapExceptionToErrorDiagnostic()
    {
        var operationId = Guid.NewGuid();
        var exception = new InvalidOperationException("Technical failure");

        var result = this.adapter.ToDiagnostic(
            exception,
            operationId,
            FailureDomain.ProjectValidation,
            DiagnosticCodes.ProjectPrefix + "INVALID",
            "Project is invalid.",
            affectedPath: "Project.oxy");

        _ = result.OperationId.Should().Be(operationId);
        _ = result.Domain.Should().Be(FailureDomain.ProjectValidation);
        _ = result.Severity.Should().Be(DiagnosticSeverity.Error);
        _ = result.Code.Should().Be(DiagnosticCodes.ProjectPrefix + "INVALID");
        _ = result.Message.Should().Be("Project is invalid.");
        _ = result.TechnicalMessage.Should().Be("Technical failure");
        _ = result.ExceptionType.Should().Be(typeof(InvalidOperationException).FullName);
        _ = result.AffectedPath.Should().Be("Project.oxy");
    }

    [TestMethod]
    public void ToDiagnostic_ShouldMapOperationCanceledExceptionToInfoDiagnostic()
    {
        var result = this.adapter.ToDiagnostic(
            new OperationCanceledException("Cancelled"),
            Guid.NewGuid(),
            FailureDomain.WorkspaceActivation,
            DiagnosticCodes.WorkspacePrefix + "CANCELLED",
            "Operation cancelled.");

        _ = result.Severity.Should().Be(DiagnosticSeverity.Info);
        _ = result.ExceptionType.Should().Be(typeof(OperationCanceledException).FullName);
    }
}
