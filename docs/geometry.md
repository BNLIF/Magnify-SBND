# SBND Magnify — Geometry Reference

All values verified against `magnify-evt*-anode{0,1}.root` from 10 events
(histogram X-axis of `h{u,v,w}_dnnsp{0,1}` and `T_bad{0,1}` trees).

## Channel ranges

Channel IDs are global and run continuously across both anodes.

### Anode 0

| Plane | chid range    | # channels |
|-------|---------------|-----------|
| U     | 0 – 1983      | 1984      |
| V     | 1984 – 3967   | 1984      |
| W     | 3968 – 5637   | 1670      |

### Anode 1

| Plane | chid range      | # channels |
|-------|-----------------|-----------|
| U     | 5638 – 7621     | 1984      |
| V     | 7622 – 9605     | 1984      |
| W     | 9606 – 11275    | 1670      |

**Total channels:** 2 × (1984 + 1984 + 1670) = **11276**

## W-plane TPC-seam dead channels

SBND is a two-TPC detector. Each anode is formed by joining two TPCs back-to-back;
the W (collection) wires that would span the cathode gap are physically unconnected
and are always flagged bad. These 6 channels appear in `T_bad{anode}` for every event.

| Anode | Dead chid range | Plane-relative wire index |
|-------|-----------------|--------------------------|
| 0     | 4800 – 4805     | 832 – 837 of 0–1669      |
| 1     | 10438 – 10443   | 832 – 837 of 0–1669      |

The seam sits just below the midpoint of the W plane (mid-wire would be between
834 and 835 for a 1670-wire plane). Both anodes have identical plane-relative
positions, as expected from the symmetric geometry.

## T_bad tree schema

Each magnify file contains a `T_bad{anodeNo}` tree with branches:

| Branch       | Type | Description                                      |
|--------------|------|--------------------------------------------------|
| `chid`       | I    | Global channel ID                                |
| `plane`      | I    | 0 = U, 1 = V, 2 = W                             |
| `start_time` | I    | First bad tick (inclusive)                       |
| `end_time`   | I    | Last bad tick (exclusive); 3427 = full readout   |

Note: `end_time = 3427` means the channel is bad for the full readout window.

## Time-tick axis

Verified stable across all 10 events, both anodes, all three planes.

| Quantity | Value |
|---|---|
| Histogram Y bins (`nbinsY`) | 3427 |
| Tick width | 0.5 µs |
| Total readout window | 3427 × 0.5 µs ≈ 1.714 ms |
| `Trun.total_time_bin` (physics readout) | 3400 |
| Extra deconvolution-tail ticks (3400–3426) | 27 — carry real signal, not empty padding |

The W plane always has signal in all 27 tail ticks; U/V do too in events
with late activity. Do not crop the display at tick 3400.

## Summary of constants (event/Geometry.h)

| Constant        | Value |
|-----------------|------:|
| N_BLOCKS        | 2     |
| N_CH_U          | 1984  |
| N_CH_V          | 1984  |
| N_CH_W          | 1670  |
| N_CH_PER_BLOCK  | 5638  |
| N_TOTAL_CH      | 11276 |
| N_TICKS         | 3400  |
