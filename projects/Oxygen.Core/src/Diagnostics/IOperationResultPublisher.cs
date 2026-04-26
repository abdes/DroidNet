// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Core.Diagnostics;

/// <summary>
/// Publishes finalized operation results to interested consumers.
/// </summary>
public interface IOperationResultPublisher : IObservable<OperationResult>
{
    /// <summary>
    /// Publishes a finalized operation result.
    /// </summary>
    /// <param name="result">The result to publish.</param>
    public void Publish(OperationResult result);
}
