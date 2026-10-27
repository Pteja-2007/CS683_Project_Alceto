# L1D Prefetcher Results: Traces 1–3

All configurations used the repository's single-core ChampSim setup and the command-line settings `-warmup_instructions 25000000 -simulation_instructions 25000000`. The five configurations are the no-prefetch baseline, IP stride, SPP, global stream, and revised Alecto (IP stride + SPP + global stream).

## Trace 1

| L1D prefetcher | IPC | Speedup vs. none | L1D load MPKI | L1D accuracy | Useful prefetches | Estimated L1D coverage |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| None (baseline) | 0.232264 | 1.0000x | 10.6786 | N/A | 0 | 0.00% |
| IP stride | 0.233313 | 1.0045x (+0.45%) | 10.1180 | 52.8814% | 16,380 | 5.25% |
| SPP | 0.232854 | 1.0025x (+0.25%) | 10.4346 | 60.8957% | 6,799 | 2.29% |
| Global stream | 0.233169 | 1.0039x (+0.39%) | 10.2163 | 42.5881% | 16,347 | 4.33% |
| Alecto | **0.234323** | **1.0089x (+0.89%)** | **9.30588** | 44.5375% | **52,303** | **12.85%** |

## Trace 2

| L1D prefetcher | IPC | Speedup vs. none | L1D load MPKI | L1D accuracy | Useful prefetches | Estimated L1D coverage |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| None (baseline) | 0.250806 | 1.0000x | 13.5412 | N/A | 0 | 0.00% |
| IP stride | 0.250650 | 0.9994x (-0.06%) | 13.4406 | 38.3574% | 4,418 | 0.74% |
| SPP | 0.250950 | 1.0006x (+0.06%) | 13.4432 | 67.4447% | 2,745 | 0.72% |
| Global stream | 0.251747 | 1.0038x (+0.38%) | 13.3366 | 31.0078% | 13,200 | 1.51% |
| Alecto | **0.252121** | **1.0052x (+0.52%)** | **12.9482** | 29.1694% | **34,263** | **4.38%** |

## Trace 3

| L1D prefetcher | IPC | Speedup vs. none | L1D load MPKI | L1D accuracy | Useful prefetches | Estimated L1D coverage |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| None (baseline) | 0.381942 | 1.0000x | 3.40372 | N/A | 0 | 0.00% |
| IP stride | 0.382799 | 1.0022x (+0.22%) | 3.31888 | 15.9422% | 2,714 | 2.49% |
| SPP | 0.382503 | 1.0015x (+0.15%) | 3.36056 | 29.3327% | 1,178 | 1.27% |
| Global stream | 0.385699 | 1.0098x (+0.98%) | 3.19080 | 44.3286% | 6,503 | 6.26% |
| Alecto | **0.390698** | **1.0229x (+2.29%)** | **2.83284** | 33.7486% | **17,095** | **16.77%** |

## Metric definitions and run notes

- Speedup is `IPC(prefetcher) / IPC(no-prefetch)` for the same trace.
- Coverage uses `(baseline L1D load misses - prefetcher L1D load misses) / baseline L1D load misses`. Baseline load misses were 266,965 for Trace 1, 338,529 for Trace 2, and 85,093 for Trace 3.
- Accuracy is ChampSim's printed `USEFUL LOAD PREFETCHES / PREFETCH ISSUED TO LOWER LEVEL`; it is not useful prefetches divided by the total count on the `L1D PREFETCH ... ISSUED` line.
- Trace 2 and Trace 3 simulator logs report 25,000,001 measured instructions after being invoked with `-simulation_instructions 25000000` (the simulator's completion count is one above the argument). Trace 1 reports 25,000,000.
- The revised Alecto version was compiled before the final Trace 1–3 runs. Results cover three supplied traces only and remain workload-specific.
