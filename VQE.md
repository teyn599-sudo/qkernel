# VQE (Variational Quantum Eigensolver)

Hamiltonian: H = -sum Z_i + sum X_i X_{i+1}

## Convergence

| qubits | params | iters | error |
|---|---|---|---|
| 2 | 4 | 156 | 2.9e-08 |
| 4 | 16 | 527 | 4.4e-04 |
| 6 | 36 | 2924 | 1.6e-03 |
| 8 | 64 | 1409 | 4.1e-04 |

## Under shot noise (1000 shots/eval, fixed budget)

Optimizer comparison:

| Optimizer | 2q | 4q | 6q |
|---|---|---|---|
| SPSA vs SGD | 550x | 195x | 43x |
| SPSA vs coordinate descent | — | 2.5x | 4x |

SPSA is the winner because it estimates the gradient from only two
function evaluations per step, regardless of parameter count. SGD
needs 2P evaluations per step (P = number of parameters).

## Source

`src/qaoa2.c` (shares optimizer code with VQE).
