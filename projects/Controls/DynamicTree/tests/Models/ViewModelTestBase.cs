// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace DroidNet.Controls.Tests;

/// <summary>
/// Base class for ViewModel tests; provides the <see cref="ILoggerFactory"/> the view models under test are created with.
/// </summary>
public abstract class ViewModelTestBase
{
    /// <summary>
    /// Gets the <see cref="ILoggerFactory"/> used by tests. It discards all output; tests assert behavior, not logs.
    /// </summary>
    protected ILoggerFactory LoggerFactoryInstance { get; } = NullLoggerFactory.Instance;
}
