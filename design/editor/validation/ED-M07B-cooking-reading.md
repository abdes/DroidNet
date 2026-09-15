# M07B Cooking transcript reading

Date: 2026-09-15

The final audit found that disabling Output's inner scrolling also disabled its
tail-follow behavior, while the shared details scroller had no per-run reading
state. The Cooking view now retains each run's offset and message selection and
responds to output/layout events through one queued restore. It adds no polling.

Rendered tests verify:

- A newly selected long transcript starts at the top.
- Scrolling to the end enables follow; appended messages move to the new end
  without clearing selected messages.
- Scrolling up suspends follow and preserves the offset while output grows.
- Switching between two runs restores each offset and selected messages.
- Hiding/reopening retains the reading state and subsequent updates remain live.
- Buffered messages from the old source never enter a newly selected transcript.
- Reloading OutputConsole does not duplicate toolbar callbacks.

OutputConsole now recreates its buffered subscriptions for each loaded/source
lifetime, ignores queued callbacks from retired sources, and exposes selection
capture/restore for transcript hosts. Its existing inner-scroll option remains
disabled in Cooking; Output and Assets still expand into the outer scroller.

The related Cooking, Main repair, import/retry, save-conflict, priority and
project-closure group passes **32/32**. Changed files have no analyzer or IDE
diagnostics.

Evidence: `artifacts/TestResults/m07b-cooking-reading-qualified.trx`.
Final build: `artifacts/m07b-cooking-reading-scoped-build.log`.
