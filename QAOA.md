# QAOA for MaxCut — Warm Start + Momentum

Graph: 4 nodes, 5 edges (cycle + diagonal)
Optimal MaxCut = 4.0

| p (layers) | <H_C> | ratio |
|---|---|---|
| 1 | 3.2371 | 80.9% |
| 2 | 3.5561 | 88.9% |
| 3 | 3.9870 | 99.7% |
| 4 | 3.9990 | 100.0% |
| 5 | 4.0000 | 100.0% |
| 6 | 4.0000 | 100.0% |
| 7 | 4.0000 | 100.0% |
| 8 | 4.0000 | 100.0% |

Method:
- Warm start: each layer p initializes from layer p-1's solution
- 50 random restarts per layer
- Momentum SGD (beta=0.9), learning rate decay
- 300 iterations per restart

Without warm start, optimization collapses at p>=5
(barren plateau). With warm start, convergence is
monotonic to the exact optimum.

Source: src/qaoa2.c
