// Copyright Epic Games, Inc. All Rights Reserved.

#include "TP_VehicleAdvWheelFront.h"
#include "UObject/ConstructorHelpers.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS

UTP_VehicleAdvWheelFront::UTP_VehicleAdvWheelFront()
{
	WheelRadius = 18.f;
	WheelWidth = 20.0f;
	LongitudinalFrictionForceMultiplier = 2.5f;
	LateralFrictionForceMultiplier = 2.0f;
	bAffectedByHandbrake = false;
	bAffectedBySteering = true;
	AxleType = EAxleType::Front;
	SpringRate = 400.0f;
	SpringPreload = 100.f;
	RollbarScaling = 0.5f;
	SuspensionMaxRaise = 8;
	SuspensionMaxDrop = 12;
	WheelLoadRatio = 0.5f;
	MaxSteerAngle = 40.f;
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS