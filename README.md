# Alceto Prefetcher Project 24B3973

This repository contains ChampSim and five L1 data prefetcher configurations: no prefetching, IP stride, SPP, global stream, and Alecto. Alecto routes each demand access to candidate predictors according to per-PC usefulness feedback.

## Build

Build a simulator by choosing the L1D prefetcher, L2 replacement policy, and binary label:

```bash
./build_champsim.sh <prefetcher> lru <label>
```

For example:

```bash
./build_champsim.sh alecto lru alecto
```

Available prefetcher names are `no`, `ip_stride`, `spp`, `global_stream`, and `alecto`.

## Run

Run the generated binary with warmup and simulation instruction counts and a trace path:

```bash
./bin/<binary> -warmup_instructions 25000000 \
  -simulation_instructions 25000000 -traces Traces/Trace1.gz
```

The build script prints the generated binary name. Replace `Trace1.gz` with `Trace2.gz` or `Trace3.gz` to run another trace.

## Traces are avaliable at 
 github link LDAP : https://drive.google.com/drive/folders/1zYchkn-M1auZp_l5wRkIzAcDYPkNtW3H?usp=sharing
