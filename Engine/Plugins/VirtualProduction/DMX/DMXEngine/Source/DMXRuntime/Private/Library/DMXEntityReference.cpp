// Copyright Epic Games, Inc. All Rights Reserved.

#include "Library/DMXEntityReference.h"
#include "Library/DMXLibrary.h"
#include "Library/DMXEntity.h"
#include "Library/DMXEntityController.h"
#include "Library/DMXEntityFixtureType.h"
#include "Library/DMXEntityFixturePatch.h"

FDMXEntityReference::FDMXEntityReference()
	: DMXLibrary(nullptr)
	, bDisplayLibraryPicker(true)
	, EntityId(0, 0, 0, 0)
{}

FDMXEntityReference::FDMXEntityReference(UDMXEntity* InEntity)
	: FDMXEntityReference()
{
	SetEntity(InEntity);
}

void FDMXEntityReference::SetEntity(const UDMXEntity* NewEntity)
{
	if (NewEntity != nullptr)
	{
		DMXLibrary = NewEntity->GetParentLibrary();
		EntityId = NewEntity->GetID();
		EntityType = NewEntity->GetClass();
	}
	else if (EntityId.IsValid())
	{
		InvalidateId();
	}
}

void FDMXEntityReference::InvalidateId()
{
	EntityId.Invalidate();
}

UDMXEntity* FDMXEntityReference::GetEntity() const
{
	if (DMXLibrary != nullptr && EntityId.IsValid())
	{
		if (UDMXEntity* Entity = DMXLibrary->FindEntity(EntityId))
		{
			if (Entity->GetClass()->IsChildOf(GetEntityType()))
			{
				return Entity;
			}
		}
	}
	return nullptr;
}

TSubclassOf<UDMXEntity> FDMXEntityReference::GetEntityType() const
{
	return EntityType;
}

bool FDMXEntityReference::operator==(const FDMXEntityReference& Other) const
{
	return DMXLibrary == Other.DMXLibrary && EntityId == Other.EntityId;
}

bool FDMXEntityReference::operator!=(const FDMXEntityReference& Other) const
{
	return !(*this == Other);
}

FDMXEntityControllerRef::FDMXEntityControllerRef()
{
	EntityType = UDMXEntityController::StaticClass();
}

FDMXEntityControllerRef::FDMXEntityControllerRef(UDMXEntityController* InController)
	: FDMXEntityReference(InController)
{}

UDMXEntityController* FDMXEntityControllerRef::GetController() const
{
	return Cast<UDMXEntityController>(GetEntity());
}

FDMXEntityFixtureTypeRef::FDMXEntityFixtureTypeRef()
{
	EntityType = UDMXEntityFixtureType::StaticClass();
}

FDMXEntityFixtureTypeRef::FDMXEntityFixtureTypeRef(UDMXEntityFixtureType* InFixtureType)
	: FDMXEntityReference(InFixtureType)
{}

UDMXEntityFixtureType* FDMXEntityFixtureTypeRef::GetFixtureType() const
{
	if (!CachedEntityFixtureType.IsValid())
	{
		CachedEntityFixtureType = Cast<UDMXEntityFixtureType>(GetEntity());
	}

	return CachedEntityFixtureType.Get();
}

FDMXEntityFixturePatchRef::FDMXEntityFixturePatchRef()
{
	EntityType = UDMXEntityFixturePatch::StaticClass();
}

FDMXEntityFixturePatchRef::FDMXEntityFixturePatchRef(UDMXEntityFixturePatch* InFixturePatch)
	: FDMXEntityReference(InFixturePatch)
{}

UDMXEntityFixturePatch* FDMXEntityFixturePatchRef::GetFixturePatch() const
{
	if (!CachedEntityFixturePatch.IsValid())
	{
		CachedEntityFixturePatch = Cast<UDMXEntityFixturePatch>(GetEntity());
	}

	return CachedEntityFixturePatch.Get();
}

//~ Type conversions extension for Entity Reference structs

UDMXEntityController* UDMXEntityReferenceConversions::Conv_ControllerRefToObj(const FDMXEntityControllerRef& InControllerRef)
{
	return InControllerRef.GetController();
}

UDMXEntityFixtureType* UDMXEntityReferenceConversions::Conv_FixtureTypeRefToObj(const FDMXEntityFixtureTypeRef& InFixtureTypeRef)
{
	return InFixtureTypeRef.GetFixtureType();
}

UDMXEntityFixturePatch* UDMXEntityReferenceConversions::Conv_FixturePatchRefToObj(const FDMXEntityFixturePatchRef& InFixturePatchRef)
{
	return InFixturePatchRef.GetFixturePatch();
}

FDMXEntityControllerRef UDMXEntityReferenceConversions::Conv_ControllerObjToRef(UDMXEntityController* InController)
{
	return FDMXEntityControllerRef(InController);
}

FDMXEntityFixtureTypeRef UDMXEntityReferenceConversions::Conv_FixtureTypeObjToRef(UDMXEntityFixtureType* InFixtureType)
{
	return FDMXEntityFixtureTypeRef(InFixtureType);
}

FDMXEntityFixturePatchRef UDMXEntityReferenceConversions::Conv_FixturePatchObjToRef(UDMXEntityFixturePatch* InFixturePatch)
{
	return FDMXEntityFixturePatchRef(InFixturePatch);
}
