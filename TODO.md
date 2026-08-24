# BoostMQ TODO

## Goal

Build a reliable BoostMQ backend for researchers without redesigning the
existing Abstract simulator.

A new algorithm should use the documented QUANTAS peer interface, run through
BoostMQ, and produce enough evidence to diagnose failures and analyse results.

## Rules

- Preserve Abstract unless the supervisor explicitly requests a change.
- Use the same researcher algorithm source where the supported interface allows it.
- Treat real process scheduling, timing, queue pressure, and message order as
  BoostMQ behaviour that must be measured and explained.
- Do not pretend that remote `Peer*` objects or shared C++ memory exist across
  BoostMQ processes.
- Use existing algorithms to validate the backend; future researcher algorithms
  are the handover target.

## Work order

1. Define and validate the supported researcher contract: #41.
2. Collect the metrics required for comparison and handover: #42.
3. Validate and document the researcher handover: #43.
4. Create and run the final Abstract-versus-BoostMQ comparison issue described
   in `Documentation/logs/mqtodo.md`.

## Done when

- Supported new algorithms build and run without BoostMQ-only algorithm code.
- Failures return a nonzero status and leave no QUANTAS processes or queues.
- Reports separate lifecycle, transport, and algorithm evidence.
- Repeated tests start with clean state.
- Documentation explains the supported interface, workflow, outputs, and limits.
- The Abstract-versus-BoostMQ comparison records whether the Abstract delay
  model approaches real BoostMQ measurements and by what percentage.
