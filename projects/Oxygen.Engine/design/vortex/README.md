# Vortex renderer

Start with the question you need to answer:

| Question                                                | Document                                                                                          |
| ------------------------------------------------------- | ------------------------------------------------------------------------------------------------- |
| What does Vortex deliver?                               | [Product requirements](PRD.md)                                                                    |
| How is the renderer organized?                          | [Architecture](ARCHITECTURE.md), [design overview](DESIGN.md), [source layout](PROJECT-LAYOUT.md) |
| What is a subsystem's contract?                         | [Low-level designs](lld/README.md)                                                                |
| What was planned, delivered or deferred?                | [Milestone roadmap](PLAN.md)                                                                      |
| What are the engineering and documentation conventions? | [Rules](RULES.md)                                                                                 |

**Current progress:** [Milestone status](STATUS.md). **Unfinished work:** [Open items](OPEN_ITEMS.md).

Each milestone has a permanent directory under `milestones/`. Its `README.md`
owns scope, task status, acceptance and outcome. Large validation records and
captured evidence live beside it. Technical contracts belong to the LLDs.

The exposure package starts at [Exposure and LightBench](milestones/exposure/README.md).
The next planned editor extension is [ED-M08](milestones/ED-M08/README.md).
[Capability boundaries](milestones/capabilities.md) distinguish the delivered
baseline from future rendering families.

For the pre-refactor documents and source-to-destination map, see the
[legacy reference](archive/README.md).
