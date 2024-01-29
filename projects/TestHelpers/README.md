# Test Helpers

## Testing code that has Debug Assertions

The `TestSuiteWithAssertions` base class can be used to handle Debug.Assert()
failures in the code under test.

Simply create a Test Suite that extends `TestSuiteWithAssertions`, and then do
the testing as usual. Assertion failures do not cause the test to terminate
anymore, instead they are recorded in the `TraceListener.RecordedMessages`
member of the test suite.

Check the recorded messaged for any messages that start with `"Fail: "` to look
for assertion failures.

> NOTE: because Debug.Assert() only gets real in DEBUG builds, always surround
> the test part checking for assertion failures with `DEBUG` guards.

```c#
#if DEBUG
_ = this.TraceListener.RecordedMessages
        .Should().Contain(message => message.StartsWith("Fail: "));
#endif
```
