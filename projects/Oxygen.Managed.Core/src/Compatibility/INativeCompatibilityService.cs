// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Core.Compatibility;

/// <summary>Verifies the installed artifact set before a native operation begins.</summary>
public interface INativeCompatibilityService
{
    /// <summary>Acquires the verified artifact leases required by the native owner.</summary>
    /// <param name="operationId">The operation correlation identity.</param>
    /// <param name="cancellationToken">Cancels verification.</param>
    /// <returns>The compatible set or diagnostics preventing native work.</returns>
    public Task<NativeCompatibilityResult> VerifyAsync(Guid operationId, CancellationToken cancellationToken);
}
