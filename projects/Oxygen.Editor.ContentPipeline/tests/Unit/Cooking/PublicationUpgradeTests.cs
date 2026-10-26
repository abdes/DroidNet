// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using static Oxygen.Editor.ContentPipeline.TestSupport.CookCoordinatorScenario;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Cooking;

/// <summary>
/// Cooked content published in a format the editor no longer reads is replaced only by a cook of the whole project the
/// user asked for: automatic cooks, preview demands and narrower cooks leave it alone.
/// </summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class PublicationUpgradeTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Only a requested project cook may upgrade the publication.</summary>
    /// <param name="kind">The requested cook scope.</param>
    /// <param name="automatic">Whether Save submitted the request.</param>
    /// <param name="demand">Whether the preview demanded it.</param>
    /// <param name="upgrades">Whether the operation may replace an outdated publication.</param>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    [DataRow(CookTargetKind.Project, false, false, true)]
    [DataRow(CookTargetKind.Project, true, false, false)]
    [DataRow(CookTargetKind.Project, false, true, false)]
    [DataRow(CookTargetKind.Folder, false, false, false)]
    [DataRow(CookTargetKind.Asset, false, false, false)]
    public async Task OnlyARequestedProjectCookUpgradesThePublication(CookTargetKind kind, bool automatic, bool demand, bool upgrades)
    {
        using var session = CreateSession();
        using var coordinator = CreateCoordinator(session);
        var scope = kind == CookTargetKind.Project ? null : AssetPath.Parse("/Content/Materials");
        bool? observed = null;

        _ = await coordinator.RunCookAsync(
            new(kind, scope, automatic) { IsDemand = demand },
            (operation, _) =>
            {
                observed = operation.UpgradesPublication;
                return Task.FromResult(new ContentCookResult(operation.OperationId, kind, OperationStatus.Succeeded, [], [], Inspection: null, Validation: null));
            },
            this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = observed.Should().Be(upgrades);
    }
}
