// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;

namespace DroidNet.Controls;

/// <summary>Parses arithmetic input without evaluating arbitrary code.</summary>
public static class NumericInputParser
{
    /// <summary>Parses an absolute expression or a relative edit expression.</summary>
    /// <param name="text">The input text.</param>
    /// <param name="currentValue">The current value used to check the result.</param>
    /// <param name="expression">The parsed expression when successful.</param>
    /// <returns><see langword="true"/> when the input has a finite result.</returns>
    public static bool TryParse(string? text, float currentValue, out NumericInputExpression expression)
    {
        expression = default;
        if (string.IsNullOrWhiteSpace(text))
        {
            return false;
        }

        var input = text.Trim();
        var operation = NumericInputOperation.Absolute;
        if (input.StartsWith("+=", StringComparison.Ordinal))
        {
            operation = NumericInputOperation.Add;
            input = input[2..];
        }
        else if (input.StartsWith("-=", StringComparison.Ordinal))
        {
            operation = NumericInputOperation.Subtract;
            input = input[2..];
        }
        else if (input[0] is '*' or '/')
        {
            operation = input[0] == '*' ? NumericInputOperation.Multiply : NumericInputOperation.Divide;
            input = input[1..];
            if (input.StartsWith('='))
            {
                input = input[1..];
            }
        }

        var parser = new ArithmeticParser(input, CultureInfo.CurrentCulture);
        if (!parser.TryParse(out var operand))
        {
            return false;
        }

        var parsed = new NumericInputExpression(operation, operand);
        var result = parsed.Apply(currentValue);
        if (!float.IsFinite(operand) || !float.IsFinite(result))
        {
            return false;
        }

        expression = parsed;
        return true;
    }

    private sealed class ArithmeticParser(string text, CultureInfo culture)
    {
        private int position;

        public bool TryParse(out float value)
        {
            value = 0;
            if (!this.TryExpression(out value))
            {
                return false;
            }

            this.SkipWhitespace();
            return this.position == text.Length;
        }

        private bool TryExpression(out float value)
        {
            if (!this.TryTerm(out value))
            {
                return false;
            }

            while (true)
            {
                this.SkipWhitespace();
                if (!this.TryReadOperator('+', '-', out var operation))
                {
                    return true;
                }

                if (!this.TryTerm(out var right))
                {
                    return false;
                }

                value = operation == '+' ? value + right : value - right;
            }
        }

        private bool TryTerm(out float value)
        {
            if (!this.TryFactor(out value))
            {
                return false;
            }

            while (true)
            {
                this.SkipWhitespace();
                if (!this.TryReadOperator('*', '/', out var operation))
                {
                    return true;
                }

                if (!this.TryFactor(out var right) || (operation == '/' && right == 0))
                {
                    return false;
                }

                value = operation == '*' ? value * right : value / right;
            }
        }

        private bool TryFactor(out float value)
        {
            this.SkipWhitespace();
            var sign = 1f;
            while (this.position < text.Length && text[this.position] is '+' or '-')
            {
                if (text[this.position++] == '-')
                {
                    sign = -sign;
                }

                this.SkipWhitespace();
            }

            if (this.position < text.Length && text[this.position] == '(')
            {
                this.position++;
                if (!this.TryExpression(out value))
                {
                    return false;
                }

                this.SkipWhitespace();
                if (this.position >= text.Length || text[this.position++] != ')')
                {
                    return false;
                }

                value *= sign;
                return true;
            }

            if (!this.TryNumber(out value))
            {
                return false;
            }

            value *= sign;
            return true;
        }

        private bool TryNumber(out float value)
        {
            this.SkipWhitespace();
            var start = this.position;
            var decimalSeparator = culture.NumberFormat.NumberDecimalSeparator;
            var digits = 0;

            while (this.position < text.Length && char.IsAsciiDigit(text[this.position]))
            {
                this.position++;
                digits++;
            }

            if (text.AsSpan(this.position).StartsWith(decimalSeparator, StringComparison.Ordinal))
            {
                this.position += decimalSeparator.Length;
                while (this.position < text.Length && char.IsAsciiDigit(text[this.position]))
                {
                    this.position++;
                    digits++;
                }
            }

            if (digits == 0)
            {
                value = 0;
                return false;
            }

            if (this.position < text.Length && text[this.position] is 'e' or 'E')
            {
                this.position++;
                if (this.position < text.Length && text[this.position] is '+' or '-')
                {
                    this.position++;
                }

                var exponentStart = this.position;
                while (this.position < text.Length && char.IsAsciiDigit(text[this.position]))
                {
                    this.position++;
                }

                if (this.position == exponentStart)
                {
                    value = 0;
                    return false;
                }
            }

            return float.TryParse(text.AsSpan(start, this.position - start), NumberStyles.Float, culture, out value);
        }

        private bool TryReadOperator(char first, char second, out char operation)
        {
            if (this.position < text.Length && text[this.position] is var candidate && (candidate == first || candidate == second))
            {
                operation = candidate;
                this.position++;
                return true;
            }

            operation = default;
            return false;
        }

        private void SkipWhitespace()
        {
            while (this.position < text.Length && char.IsWhiteSpace(text[this.position]))
            {
                this.position++;
            }
        }
    }
}
