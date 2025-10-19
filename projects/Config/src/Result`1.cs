// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config;

/// <summary>
///     Represents the outcome of an operation: either <see langword="Success"/> with a value, or <see langword="Failure"/> with an error.
/// </summary>
/// <typeparam name="T">The type of the successful result value.</typeparam>
/// <remarks>
///     This type is inspired by Rust's <c>Result&lt;T, E&gt;</c> and provides an explicit, null-free way to represent
///     success or failure in C#.
///     <para>
///     Use <see cref="Result.Ok{T}(T)"/> to construct a successful result and <see cref="Result.Fail{T}(Exception)"/> to
///     construct a failed result. Consumers can then use <see cref="IsSuccess"/> / <see cref="IsFailure"/>, pattern matching, or
///     helper methods like <see cref="Map"/>, <see cref="Bind"/>, and <see cref="GetOrElse(Func{Exception, T})"/> to work with
///     results.
///     </para>
/// </remarks>
public readonly struct Result<T>(T? value, Exception? error) : System.IEquatable<Result<T>>
{
    /// <summary>
    ///     Gets a value indicating whether the result represents success.
    /// </summary>
    /// <example>
    ///     <code><![CDATA[
    ///     var result = Result<int>.Ok(42);
    ///     if (result.IsSuccess)
    ///     {
    ///         Console.WriteLine(result.Value); // 42
    ///     }
    ///     ]]></code>
    /// </example>
    public bool IsSuccess { get; } = error is null;

    /// <summary>
    ///     Gets a value indicating whether the result represents failure.
    /// </summary>
    /// <example>
    ///     <code><![CDATA[
    ///     var result = Result<int>.Fail(new InvalidOperationException("Bad input"));
    ///     if (result.IsFailure)
    ///     {
    ///         Console.WriteLine(result.Error.Message); // "Bad input"
    ///     }
    ///     ]]></code>
    /// </example>
    public bool IsFailure => !this.IsSuccess;

    /// <summary>
    ///     Gets the successful value if <see cref="IsSuccess"/> is <see langword="true"/>; otherwise throws
    ///     <see cref="InvalidOperationException"/>.
    /// </summary>
    public T Value => this.IsSuccess
        ? value!
        : throw new InvalidOperationException("No value present for a failed result.");

    /// <summary>
    ///     Gets the error if <see cref="IsFailure"/> is <see langword="true"/>; otherwise throws
    ///     <see cref="InvalidOperationException"/>.
    /// </summary>
    public Exception Error => this.IsFailure
        ? error!
        : throw new InvalidOperationException("No error present for a successful result.");

    /// <summary>
    ///     Equality operator.
    /// </summary>
    /// <param name="left">The first <see cref="Result{T}"/> to compare.</param>
    /// <param name="right">The second <see cref="Result{T}"/> to compare.</param>
    public static bool operator ==(Result<T> left, Result<T> right) => left.Equals(right);

    /// <summary>
    ///     Inequality operator.
    /// </summary>
    /// <param name="left">The first <see cref="Result{T}"/> to compare.</param>
    /// <param name="right">The second <see cref="Result{T}"/> to compare.</param>
    public static bool operator !=(Result<T> left, Result<T> right) => !(left == right);

    /// <summary>
    /// Equality comparison with another Result{T}.
    /// </summary>
    /// <param name="other">The <see cref="Result{T}"/> instance to compare with this result.</param>
    /// <returns><see langword="true"/> if the results are equal; otherwise, <see langword="false"/>.</returns>
    public bool Equals(Result<T> other)
        => this.IsSuccess == other.IsSuccess
           && (this.IsSuccess
               ? EqualityComparer<T>.Default.Equals(value, other.Value)
               : EqualityComparer<Exception>.Default.Equals(this.Error, other.Error));

    /// <summary>
    ///     Overrides object.Equals.
    /// </summary>
    /// <param name="obj">The object to compare with the current <see cref="Result{T}"/>.</param>
    /// <returns>
    ///     <see langword="true"/> if the specified object is equal to the current <see cref="Result{T}"/>; otherwise,
    ///     <see langword="false"/>.
    /// </returns>
    public override bool Equals(object? obj) => obj is Result<T> other && this.Equals(other);

    /// <summary>
    ///     Returns a hash code for this instance.
    /// </summary>
    /// <remarks>
    ///     The hash code combines the success/failure state with the hash code of the underlying value or error. If the
    ///     value or error is <see langword="null"/>, zero is used.
    /// </remarks>
    /// <returns>A hash code for this instance.</returns>
    public override int GetHashCode()
        => HashCode.Combine(this.IsSuccess, this.IsSuccess ? (object?)this.Value : this.Error);

    /// <summary>
    ///     Deconstructs the result into its components for use in pattern matching.
    /// </summary>
    /// <param name="isSuccess">Whether the result is successful.</param>
    /// <param name="val">The value if successful, otherwise <c>default</c>.</param>
    /// <param name="err">The error if failed, otherwise <c>null</c>.</param>
    /// <example>
    ///     <code><![CDATA[
    ///     var result = Result<int>.Ok(10);
    ///     var (ok, value, error) = result;
    ///     Console.WriteLine(ok);    // True
    ///     Console.WriteLine(value); // 10
    ///     ]]></code>
    /// </example>
    public void Deconstruct(out bool isSuccess, out T? val, out Exception? err)
    {
        isSuccess = this.IsSuccess;
        val = value;
        err = error;
    }

    /// <summary>
    ///     Maps a successful value into another <see cref="Result{TMapped}"/>, preserving errors.
    /// </summary>
    /// <typeparam name="TMapped">The type of the mapped value.</typeparam>
    /// <param name="f">The mapping function to apply if successful.</param>
    /// <returns>A new <see cref="Result{TMapped}"/>.</returns>
    /// <example>
    ///     <code><![CDATA[
    ///     var result = Result<int>.Ok(5).Map(x => x * 2);
    ///     Console.WriteLine(result.Value); // 10
    ///     ]]></code>
    /// </example>
    public Result<TMapped> Map<TMapped>(Func<T, TMapped> f)
        => this.IsSuccess ? Result.Ok(f(this.Value)) : Result.Fail<TMapped>(this.Error);

    /// <summary>
    ///     Chains computations that return <see cref="Result{U}"/> (monadic bind).
    /// </summary>
    /// <typeparam name="TBindResult">The type of the result value returned by the function.</typeparam>
    /// <param name="f">The function to apply if successful.</param>
    /// <returns>The result of the function if successful; otherwise the original error.</returns>
    /// <example>
    ///     <code><![CDATA[
    ///     Result<int> Divide(int a, int b)
    ///         => b == 0 ? Result<int>.Fail(new DivideByZeroException())
    ///                   : Result<int>.Ok(a / b);
    ///
    ///     var result = Result<int>.Ok(10).Bind(x => Divide(x, 2));
    ///     Console.WriteLine(result.Value); // 5
    ///     ]]></code>
    /// </example>
    public Result<TBindResult> Bind<TBindResult>(Func<T, Result<TBindResult>> f)
        => this.IsSuccess ? f(this.Value) : Result.Fail<TBindResult>(this.Error);

    /// <summary>
    ///     Maps the error into another <see cref="Exception"/>, preserving success.
    /// </summary>
    /// <param name="f">The mapping function to apply to the error.</param>
    /// <returns>A new <see cref="Result{T}"/> with the mapped error if failed; otherwise the original result.</returns>
    /// <example>
    ///     <code><![CDATA[
    ///     var result = Result<int>.Fail(new InvalidOperationException("Bad input"))
    ///         .MapError(e => new ApplicationException("Wrapped", e));
    ///     Console.WriteLine(result.Error.GetType().Name); // ApplicationException
    ///     ]]></code>
    /// </example>
    public Result<T> MapError(Func<Exception, Exception> f)
        => this.IsFailure ? Result.Fail<T>(f(this.Error)) : this;

    /// <summary>
    ///     Provides a fallback value if the result is a failure.
    /// </summary>
    /// <param name="fallback">The fallback value to return if failed.</param>
    /// <returns>The successful value or the fallback.</returns>
    /// <example>
    ///     <code><![CDATA[
    ///     var result = Result<int>.Fail(new Exception("oops"));
    ///     Console.WriteLine(result.GetOrElse(99)); // 99
    ///     ]]></code>
    /// </example>
    public T GetOrElse(T fallback)
        => this.IsSuccess ? this.Value : fallback;

    /// <summary>
    ///     Provides a fallback computed from the error if the result is a failure.
    /// </summary>
    /// <param name="fallbackFactory">A function that produces a fallback value from the error.</param>
    /// <returns>The successful value or the computed fallback.</returns>
    /// <example>
    ///     <code><![CDATA[
    ///     var result = Result<int>.Fail(new Exception("oops"));
    ///     int value = result.GetOrElse(err => err.Message.Length);
    ///     Console.WriteLine(value); // 4
    ///     ]]></code>
    /// </example>
    public T GetOrElse(Func<Exception, T> fallbackFactory)
        => this.IsSuccess ? this.Value : fallbackFactory(this.Error);

    /// <summary>
    ///     Executes side-effects depending on success or failure, returning the original result.
    /// </summary>
    /// <param name="onSuccess">Action to invoke if successful.</param>
    /// <param name="onFailure">Optional action to invoke if failed.</param>
    /// <returns>The original result, enabling fluent chaining.</returns>
    /// <example>
    ///     <code><![CDATA[
    ///     var result = Result<int>.Ok(42)
    ///         .Tap(v => Console.WriteLine($"Success: {v}"),
    ///              e => Console.WriteLine($"Error: {e.Message}"));
    ///     // Prints "Success: 42"
    ///     ]]></code>
    /// </example>
    public Result<T> Tap(Action<T> onSuccess, Action<Exception>? onFailure = null)
    {
        if (this.IsSuccess)
        {
            onSuccess(this.Value);
        }
        else
        {
            onFailure?.Invoke(this.Error);
        }

        return this;
    }
}

/// <summary>
///     Provides helper factory methods for constructing <see cref="Result{T}"/> instances.
/// </summary>
/// <remarks>
///     Use <see cref="Ok{T}(T)"/> to create a successful result and <see cref="Fail{T}(Exception)"/>
///     to create a failed result containing an <see cref="Exception"/>.
/// </remarks>
public static class Result
{
    /// <summary>
    ///     Creates a successful result containing the specified value.
    /// </summary>
    /// <typeparam name="T">The type of the successful result value.</typeparam>
    /// <param name="value">The value to wrap in a successful result.</param>
    /// <returns>A <see cref="Result{T}"/> in the successful state containing the provided value.</returns>
    public static Result<T> Ok<T>(T value) => new(value, error: null);

    /// <summary>
    ///     Creates a failed result containing the specified error.
    /// </summary>
    /// <typeparam name="T">The type of the successful result value.</typeparam>
    /// <param name="error">The error to wrap in the failed result.</param>
    /// <returns>A <see cref="Result{T}"/> in the failed state containing the provided error.</returns>
    public static Result<T> Fail<T>(Exception error) => new(default, error);
}
