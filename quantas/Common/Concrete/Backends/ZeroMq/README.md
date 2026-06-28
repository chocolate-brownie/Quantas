# ConcreteZMQ

Future ZeroMQ process backend for QUANTAS.

This folder is intentionally scaffolded before implementation so the backend
architecture is visible while ConcreteMQ is being cleaned up.

Expected layout:
- `Entrypoints/`: peer and leader executable entrypoints.
- `Control/`: ZeroMQ control-plane lifecycle protocol.
- `Transport/`: ZeroMQ data-plane network interface.
