# V0.1 recovery plan

Status: `active`

Last updated: 2026-11-07

## Purpose

This plan is the one order of work from today to the V0.1 release gate. It
covers the five engine review sets not yet remediated, the open slices of
[portable references](../../../projects/Oxygen.Engine/design/proposals/portable-references/plan.md),
and the remaining [ED-M09 ledger](ED-M09-v01-gaps-and-blocking-issues.md). It
removes nothing from any of them.

Those documents still own what each step contains. This plan owns the order, the
current state and the rules for running the work.

## Where things stand

Checked against the code and git on 2026-11-07, without building or running
anything.

| Work                                   | State                                                                                                                                             |
| -------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| R1 to R9, R11, R12, R16                | Remediated, from their commits. R1, R2 and R7 have no written record of which findings closed, and findings were not rechecked one by one.        |
| R15                                    | Prompt issued; no report or remediation found.                                                                                                    |
| R13, R10, R14, R17                     | Not reviewed.                                                                                                                                     |
| Texture slots S1 to S4                 | Committed. #113 and #107 close at the merge to `master`.                                                                                          |
| Portable references S0 to S13          | Committed. S7 to S9 listed failures owned by later slices.                                                                                        |
| Portable references S14, S15           | Committed, not proven. Their window lanes have not run, and Retarget has not been confirmed in the editor.                                        |
| Portable references S18                | Committed.                                                                                                                                        |
| Portable references S19                | Script exists and the owner's projects are migrated. Everything else in the slice is open.                                                        |
| Portable references S16, S17, S20, S21 | Not started.                                                                                                                                      |
| ContentPipeline integration            | 60 of 578 fail. 18 of them mostly report only an unpublished cook, so their cause is unknown. Last run before S14's final fixes; not run for S15. |
| Branch                                 | Pushed to `origin`. Nothing reaches `master` before step 13.                                                                                      |

## Order

| Step | Work                                     | Why it is here                                                                                  | Estimated sessions | State       | Sessions used | Proof |
| ---- | ---------------------------------------- | ----------------------------------------------------------------------------------------------- | ------------------ | ----------- | ------------- | ----- |
| 1    | Prove S14 and S15                        | Nothing more is built on unproven work.                                                         | 1                  | Not started | -             | -     |
| 2    | S20a: the 60 integration failures        | They may hide product defects in S7 to S15. Once green, the editor lanes gate every later step. | 2 to 3             | Not started | -             | -     |
| 3    | R15 review and remediation               | S16 builds on the pak tools.                                                                    | 4 to 6             | Not started | -             | -     |
| 4    | S16 pak build                            | Follows R15 while it is fresh.                                                                  | 1                  | Not started | -             | -     |
| 5    | R13 review and remediation               | S19 cooks scenes and physics.                                                                   | 3 to 5             | Not started | -             | -     |
| 6    | R10 review and remediation               | S19 cooks textures. It also covers the new DDS decoder.                                         | 3 to 5             | Not started | -             | -     |
| 7    | R14 review and remediation               | S19 cooks scripts and input.                                                                    | 2 to 3             | Not started | -             | -     |
| 8    | R17 review and remediation               | Last review, so it sees the kits after the other remediations changed them.                     | 2 to 3             | Not started | -             | -     |
| 9    | S17 asset packages                       | S19 requires it. Comes after the engine contracts settle.                                       | 2                  | Not started | -             | -     |
| 10   | S19 migration and studio example         | Requires S13, S16 and S17, and settled cooks.                                                   | 2                  | Not started | -             | -     |
| 11   | S20b: studio tests and the full lane run | Requires the studio example from S19.                                                           | 1                  | Not started | -             | -     |
| 12   | S21 docs sweep                           | Runs when the forms it sweeps for can no longer change.                                         | 1                  | Not started | -             | -     |
| 13   | Merge to `master`                        | The plan's condition: every lane of every touched module passes.                                | 0                  | Not started | -             | -     |
| 14   | Rest of ED-M09, ED-M10, ED-M10A          | In their ledger order. The format window (items 40 to 42) lands on reviewed code.               | Not yet sized      | Not started | -             | -     |
| 15   | ED-M11 release qualification             | The V0.1 gate.                                                                                  | Not yet sized      | Not started | -             | -     |

Steps 1 to 13 are 24 to 33 sessions. This is an estimate from earlier work: R9, R11,
R12 and R16 took 4 to 6 slices each. S18, S14 and S15 cost about 25% of the owner's
quota, including test runs that repeated. At 4% to 8% a session, steps 1 to 13 cost
about one to two and a half times the quota that 25% was measured against.

Steps 14 and 15 are not sized because their items have not been read. Each is sized
before it starts, from reading them.

A step is `Done` only when its row's Proof names its commits and the lanes that
passed, with their counts. A check the owner has not made yet leaves it `Waiting for
owner`. Steps run in this order. Changing the order or a step's scope is a commit to
this file that says why, made before the work changes.

## Steps

### 1. Prove S14 and S15

- Run the ContentExplorer UI, LivePreview Unit.UI, CookedInspector Unit.UI and
  Problems lanes once, with windows on Display 2, and fix what fails.
- Add a test that invokes the Import menu command, which S13 left untested.
- `UnavailableMountTreeItemAdapter`: S14 says to delete the binary adapter, and the
  class now shows Not bound, Invalid and missing. Decide from the design, then change
  the code or the plan's text so the two agree.
- Check whether ED-M09 item 17.7's consumers are done, since later commits moved
  cooked content onto Serio. Correct its row, and its "Next" line.

Done when these lanes pass and the owner has checked Add library and Retarget in the
editor.

### 2. S20a: the 60 integration failures

S20 is split, because only its studio tests need S19. This step is everything in S20
except those tests.

- Run ContentPipeline integration once and classify every failure against the plan's
  test impact table.
- A product defect is fixed here, in the slice's module, with a test at the lowest
  level that reaches it.
- Fixtures move onto the S1 writers, with their remaining inline JSON.

Done when every windowless and WinUI lane of every touched module passes, and the
managed analyzers are clean. The owner runs the analyzer pass from a command I give.
From here on, a failing editor lane stops the step that caused it.

### 3 and 5 to 8. Review and remediation

Each set follows the same steps:

1. Check the set's section against the code, correct it, and give the owner one
   complete prompt.
2. Verify each finding of the report against the code.
3. Make the engineering decisions. Only UX decisions go to the owner.
4. Fix in slices. Each ends with its lanes passing, the SDK installed, and the
   Showcase recooked, packed and played.

Prompts can go out together, so reports wait in hand. Remediation runs one set at a
time.

Assigned to these steps:

- Step 3: `--non-deterministic` changes nothing beyond the report, and PakDump has no
  tests. Both are checked if the report does not raise them.
- Step 6: the dark rectangular patches in Sponza's FBX that its glTF lacks. The cause
  is unknown, and one instrumented run comes first. If it is not a texture defect, it
  goes to the module that owns it, in this step.

### 4. S16 pak build

As the plan states: `PakBuildService`, the mount pre-check, the cook before packing,
the dialog and the Cook button's command.

### 9. S17 asset packages

As the plan states: export, import and replace, with the schema.

### 10. S19 migration and studio example

As the plan states, including the script's other descriptor kinds and import
manifests, Showcase as a library project, `Examples/Studio`, and cooks of Showcase, F1
Validation, Instancing and Racer.

F1 Validation's `skybox.otex` and `pisa.otex` name assets the project never had. I
look for their sources, in the backup first, and repoint them. If no source exists,
the references are removed and the report says so.

### 11. S20b: studio tests and the full lane run

The integration tests L4, L9, L10 and S9, then every lane of every touched module.

### 12. S21 docs sweep

As the plan states.

### 13. Merge

Merge when step 11's run holds after step 12, and push `master` on the owner's word.
That closes #81 and the other issues whose commits say `Fixes`.

### 14. Rest of ED-M09, ED-M10, ED-M10A

The ledger's order stands. Item 41 keeps #114, #111 and #98, and takes Bistro's packed
ORM map, which an FBX Specular slot drops today. #113 and #107 are done there.

## Rules for running this plan

- **One step at a time, one slice per session.** A session's brief names the step and
  the slice, and says to stop at its end. Going further takes a new brief.
- **Scope stated first.** Each session opens by naming its step, what is left, and
  the cost. A step that passes its estimate is reported when it does, with the new
  estimate.
- **Commits name their step.** Each commit body names its step here, so a commit
  that fits no step stands out in the log.
- **Supervision.** I read each session's report against its brief. Work outside the
  brief stops the session, and I tell the owner at once, not as routine status.
- **Decisions.** Order, scope and engineering are mine. Nothing in this plan is
  dropped or moved past the merge without the owner's approval.
- **Test runs.** One filtered run per change. Repeating a full run needs a reason
  stated first.
- **One status.** This file's Order table is updated and committed as each step ends,
  before the next step starts.

## What the owner does

- Paste each review prompt into the reviewer, and each report back.
- Check Add library and Retarget in the editor at step 1, and any later visual fix.
- Run tidy and the analyzer pass from the commands I give.
- Rebuild or reconfigure a build tree when one needs it.
- Decide UX questions, and say when to merge and push.
