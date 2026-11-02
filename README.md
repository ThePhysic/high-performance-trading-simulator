# High-Performance Trading Simulator

A C++20 event-processing engine and miniature exchange, operated through a terminal. Linux is the target environment for execution, profiling, and final performance evidence.

Implementation is in progress. The [goal](GOAL.md) and original [requirements](docs/requirements/README.md) define acceptance. Current build, validation, review, and publication evidence will be recorded in this repository; no unexecuted checks are claimed as passing.

The core combines ordered command admission, price-time matching, simulated risk reservations, canonical outcomes, and deterministic replay. CLI modes provide interactive simulation, command-file execution, replay verification, and workload benchmarks. TCP ingress and durable recovery remain optional.

Live trading, real funds, strategy research, a web interface, and a cloud stack are outside scope.
