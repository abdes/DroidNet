// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Core.Diagnostics;

namespace Oxygen.Editor.ContentBrowser.Tests;

[TestClass]
public sealed class AssetsViewModelTests
{
    [TestMethod]
    public void ShouldShowSucceededOperationResult_WhenInspectOrValidate_ShouldReturnTrue()
    {
        Assert.IsTrue(AssetsViewModel.ShouldShowSucceededOperationResult(ContentPipelineOperationKinds.CookedOutputInspect));
        Assert.IsTrue(AssetsViewModel.ShouldShowSucceededOperationResult(ContentPipelineOperationKinds.CookedOutputValidate));
    }

    [TestMethod]
    public void ShouldShowSucceededOperationResult_WhenBackgroundCookOperation_ShouldReturnFalse()
    {
        Assert.IsFalse(AssetsViewModel.ShouldShowSucceededOperationResult(ContentPipelineOperationKinds.CookProject));
    }
}
