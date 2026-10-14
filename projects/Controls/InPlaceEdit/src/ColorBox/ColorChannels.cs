// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Runtime.InteropServices;

namespace DroidNet.Controls;

/// <summary>The red, green, blue and alpha channels of a <see cref="ColorBox"/>, as stored.</summary>
/// <param name="Red">The red channel.</param>
/// <param name="Green">The green channel.</param>
/// <param name="Blue">The blue channel.</param>
/// <param name="Alpha">The straight alpha channel.</param>
[StructLayout(LayoutKind.Auto)]
public readonly record struct ColorChannels(double Red, double Green, double Blue, double Alpha)
{
    /// <summary>Gets the red, green and blue channels, as single-precision values.</summary>
    /// <returns>The channels in X, Y and Z.</returns>
    public Vector3 ToVector3() => new((float)this.Red, (float)this.Green, (float)this.Blue);

    /// <summary>Gets the four channels, as single-precision values.</summary>
    /// <returns>The channels in X, Y, Z and W.</returns>
    public Vector4 ToVector4() => new((float)this.Red, (float)this.Green, (float)this.Blue, (float)this.Alpha);
}
