// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentBrowser.Tests;

/// <summary>Checks concise operation feedback in the content browser.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class AssetsViewModelOperationResultTests
{
    /// <summary>The summary uses the actionable diagnostic and leaves technical output in diagnostics.</summary>
    [TestMethod]
    public void OperationSummaryUsesActionableMessageInsteadOfStackTrace()
    {
        var message = AssetsViewModel.BuildOperationMessage(
            "Cook failed for the active project.",
            [
                new DiagnosticRecord
                {
                    OperationId = Guid.NewGuid(),
                    Domain = FailureDomain.AssetImport,
                    Severity = DiagnosticSeverity.Error,
                    Code = AssetImportDiagnosticCodes.ImportFailed,
                    Message = "Material 'Paint' has an invalid alpha mode.",
                    TechnicalMessage = "System.InvalidOperationException: Invalid alpha mode.\n   at NativeImport()",
                },
            ]);

        _ = message.Should().Be("Cook failed for the active project. Material 'Paint' has an invalid alpha mode.");
    }
}
