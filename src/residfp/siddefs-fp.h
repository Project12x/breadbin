//  ---------------------------------------------------------------------------
//  This file is part of reSID, a MOS6581 SID emulator engine.
//  Copyright (C) 1999  Dag Lem <resid@nimrod.no>
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//  ---------------------------------------------------------------------------
//  Generated for Breadbin VST - MSVC standalone build

#ifndef SIDDEFS_FP_H
#define SIDDEFS_FP_H

// Compilation configuration - disabled for MSVC portability
#define RESID_BRANCH_HINTS 0

// Compiler specifics - MSVC doesn't have __builtin_expect
#define HAVE_BUILTIN_EXPECT 0

// Branch prediction macros (disabled on MSVC)
#define likely(x) (x)
#define unlikely(x) (x)

namespace reSIDfp {

typedef enum { MOS6581 = 1, MOS8580 } ChipModel;

typedef enum { AVERAGE = 1, WEAK, STRONG } CombinedWaveforms;

typedef enum { DECIMATE = 1, RESAMPLE } SamplingMethod;
} // namespace reSIDfp

extern "C" {
#ifndef __VERSION_CC__
extern const char *residfp_version_string;
#else
const char *residfp_version_string = "2.16.0";
#endif
}

// Inlining enabled for performance
#define RESID_INLINING 1
#define RESID_INLINE inline

#endif // SIDDEFS_FP_H
