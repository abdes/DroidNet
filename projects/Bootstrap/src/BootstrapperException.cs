// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Samples.Bootstrap;

using System;

public sealed class BootstrapperException : Exception
{
    public BootstrapperException(string message)
        : base(message)
    {
    }

    public BootstrapperException(string message, Exception innerException)
        : base(message, innerException)
    {
    }

    public BootstrapperException()
        : base("could not identify the main assembly")
    {
    }
}
