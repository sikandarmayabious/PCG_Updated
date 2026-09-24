// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGCommon.h"

#if PCG_PROFILING_ENABLED

class IPCGGraphExecutionSource;
enum class EPCGGenerationStatus : uint8;

namespace PCGProfilingLog
{
    bool IsEnabled();

    /** Emits a per-node timing table to the output log when logging is enabled. */
    void LogProfilingData(IPCGGraphExecutionSource* InExecutionSource, EPCGGenerationStatus InStatus);
}

#endif // PCG_PROFILING_ENABLED
