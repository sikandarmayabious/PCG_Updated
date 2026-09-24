// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/IConsoleManager.h"

namespace PCGLandscapeCVars
{
	PCG_API extern TAutoConsoleVariable<int> CVarLandscapeRefreshTimeDelay;
	PCG_API extern TAutoConsoleVariable<bool> CVarLandscapeDisableRefreshTracking;
	PCG_API extern TAutoConsoleVariable<bool> CVarLandscapeDisableRefreshTrackingInLandscapeEditingMode;
	PCG_API extern TAutoConsoleVariable<bool> CVarLandscapeForceRefreshRuntimeGen;
}