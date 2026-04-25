// SBND geometry constants.
// Verify all values against your actual WCT magnify ROOT file before use
// (open the file in ROOT: f.ls() and Trun->Print()).
#ifndef GEOMETRY_H
#define GEOMETRY_H

static const int N_BLOCKS       = 2;     // number of TPCs
static const int N_CH_U         = 1984;  // U wires per TPC
static const int N_CH_V         = 1984;  // V wires per TPC
static const int N_CH_W         = 1670;  // Y/W wires per TPC (collection plane) — verified against hw_dnnsp{0,1} Xaxis
static const int N_CH_PER_BLOCK = N_CH_U + N_CH_V + N_CH_W;   // 5632
static const int N_TOTAL_CH     = N_BLOCKS * N_CH_PER_BLOCK;   // 11264
static const int N_TICKS        = 3400;
static const int ADC_BITS       = 12;    // range 0..4095
static const int STICKY_PERIOD  = 64;

// Physical constants (used by test_feature/evd only)
static const double WIRE_PITCH_MM    = 3.0;   // mm
static const double TICK_US          = 0.5;   // µs per tick
static const double DRIFT_MM_PER_US  = 1.6;   // mm/µs at ~500 V/cm

// Plane letter convention — must match WCT histogram prefixes (hu_/hv_/hw_)
static const char* PLANE_NAMES[] = {"u", "v", "w"};

#endif
