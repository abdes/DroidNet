// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace DroidNet.Controls;

/// <summary>Describes how a numeric input relates to the current value.</summary>
public enum NumericInputOperation
{
    /// <summary>Replaces the current value.</summary>
    Absolute,

    /// <summary>Adds the operand to the current value.</summary>
    Add,

    /// <summary>Subtracts the operand from the current value.</summary>
    Subtract,

    /// <summary>Multiplies the current value by the operand.</summary>
    Multiply,

    /// <summary>Divides the current value by the operand.</summary>
    Divide,
}

/// <summary>A parsed numeric edit expression.</summary>
/// <param name="Operation">The operation to apply.</param>
/// <param name="Operand">The parsed operand.</param>
[StructLayout(LayoutKind.Auto)]
public readonly record struct NumericInputExpression(NumericInputOperation Operation, float Operand)
{
    /// <summary>Gets a value indicating whether this expression is relative to the current value.</summary>
    public bool IsRelative => this.Operation != NumericInputOperation.Absolute;

    /// <summary>Applies this expression to the specified current value.</summary>
    /// <param name="currentValue">The value before the edit.</param>
    /// <returns>The resulting value.</returns>
    public float Apply(float currentValue)
        => this.Operation switch
        {
            NumericInputOperation.Absolute => this.Operand,
            NumericInputOperation.Add => currentValue + this.Operand,
            NumericInputOperation.Subtract => currentValue - this.Operand,
            NumericInputOperation.Multiply => currentValue * this.Operand,
            NumericInputOperation.Divide => currentValue / this.Operand,
            _ => throw new InvalidOperationException($"Unsupported numeric input operation: {this.Operation}."),
        };
    }
