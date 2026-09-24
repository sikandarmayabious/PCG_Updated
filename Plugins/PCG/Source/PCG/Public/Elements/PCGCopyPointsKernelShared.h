// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class UPCGComputeKernel;
class UPCGDataBinding;
struct FPCGContext;

namespace PCGCopyPointsKernel
{
	/** Performs data validation common to all copy points kernels. */
	bool IsKernelDataValid(const UPCGComputeKernel* InKernel, const UPCGDataBinding* InDataBinding, FPCGContext* InContext = nullptr);
}
