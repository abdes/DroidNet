// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Moq;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Shell.Unit.Tests.Documents;

/// <summary>The tabs of a test's windows, with the close coordinator a project session gives them.</summary>
internal static class Tabs
{
    /// <summary>Creates tabs whose closes ask nothing: no editor takes part in them.</summary>
    /// <param name="loggerFactory">The logger factory, if any.</param>
    /// <returns>The tabs.</returns>
    public static DocumentTabs New(ILoggerFactory? loggerFactory = null)
        => new(new DocumentCloseCoordinator(Mock.Of<IDocumentClosePrompt>(), Mock.Of<IOperationResultPublisher>()), loggerFactory);
}
