# Deterministic event scheduling

`Scheduler` stores absolute 64-bit logical tick timestamps. These are an
execution-order contract, not calibrated EE cycles or wall-clock time.
Events contain a timer/DMA/input type and an opaque 64-bit device payload.
They contain no host function pointers or callbacks. Consumers interpret the
payload and dispatch an event to its owning device.

`schedule(tick, type, payload)` accepts current or future timestamps, returns
a monotonically issued sequence, and orders events by `(tick, sequence)`.
Same-tick events therefore execute in insertion order, including events added
while handling another event at that tick. Sequence exhaustion throws before
mutation; identifiers never wrap or get reused until reset.

`pop_next_until(target)` uses an **inclusive** deadline: if an event is due at
or before the target, it removes that event and advances the clock to its tick.
Otherwise it advances to the target and returns no event. Call repeatedly until
no event is returned to finish a bounded advance. A deadline equal to the
current tick can dispatch current-tick events. This differs deliberately from
the original `Machine::advance` input playback's half-open interval; callers
must not substitute the two APIs without translating that boundary contract.

Snapshots include the clock, next sequence, and all queued events. Restore
rejects past events, unknown types, unordered queues, duplicate sequences, and
unissued sequence numbers without modifying the live scheduler. The snapshot
is an in-memory value, not a portable save-file format. A complete machine save
must also include CPU, memory, device state, and external-input cursors. A
scheduler snapshot alone does not restore a running emulated console.

The queue uses a sorted vector to keep state inspection straightforward. No
threading, cancellation, hardware priority arbitration, periodic event policy,
or maximum-event budget is implied. Device handlers must avoid scheduling an
unbounded chain of events at one timestamp. Tests cover known event ordering,
deadline boundaries, restored replay, chunk-independent results, malformed
state rejection, and the maximum timestamp/sequence boundaries.
