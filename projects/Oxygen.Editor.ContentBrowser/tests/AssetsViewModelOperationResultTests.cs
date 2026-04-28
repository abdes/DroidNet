// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Core.Diagnostics;

namespace Oxygen.Editor.ContentBrowser.Tests;

[TestClass]
public sealed class AssetsViewModelOperationResultTests
{
    [TestMethod]
    public void BuildOperationMessage_WhenDiagnosticHasTechnicalDetails_ShouldExposeDetails()
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
                    Message = "Native content import failed.",
                    TechnicalMessage = "Material descriptor validation failed: alpha_mode is invalid.",
                },
            ]);

        _ = message.Should().Contain("Cook failed for the active project.");
        _ = message.Should().Contain("Material descriptor validation failed");
    }
}
