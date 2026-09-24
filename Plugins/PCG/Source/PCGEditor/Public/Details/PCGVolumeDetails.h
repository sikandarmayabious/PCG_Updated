// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Details/PCGComponentDetails.h"

class IDetailCustomization;

class FPCGVolumeDetails : public FPCGComponentDetails
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

protected:
	/** ~Begin FPCGComponentDetails interface */
	virtual TArray<TWeakInterfacePtr<IPCGGraphExecutionSource>> GatherExecutionSourcesFromSelection(const TArray<TWeakObjectPtr<UObject>>& InObjectSelected) const override;
	virtual bool AddDefaultProperties() const override { return false; }
	/** ~End FPCGComponentDetails interface */
};
