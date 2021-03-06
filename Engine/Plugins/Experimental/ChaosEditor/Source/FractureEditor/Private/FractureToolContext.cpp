// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureToolContext.h"

#include "GeometryCollection/GeometryCollectionClusteringUtility.h"


FFractureToolContext::FFractureToolContext(UGeometryCollectionComponent* InGeometryCollectionComponent)
{
	GeometryCollectionComponent = InGeometryCollectionComponent;
	FGeometryCollectionEdit RestCollection = GeometryCollectionComponent->EditRestCollection(GeometryCollection::EEditUpdate::None);
	FracturedGeometryCollection = RestCollection.GetRestCollection();

	Transform = GeometryCollectionComponent->GetOwner()->GetActorTransform();

	FGeometryCollectionEdit GeometryCollectionEdit = GeometryCollectionComponent->EditRestCollection();
	if (UGeometryCollection* GeometryCollectionObject = GeometryCollectionEdit.GetRestCollection())
	{
		GeometryCollection = GeometryCollectionObject->GetGeometryCollection();
		SelectedBones = GeometryCollectionComponent->GetSelectedBones();
	}
}


void FFractureToolContext::Sanitize()
{
	// Ensure that children of a selected node are not also selected.
	SelectedBones.RemoveAll([this](int32 Index) {
		return HasSelectedAncestor(Index);
		});

	SelectedBones.Sort();
}

void FFractureToolContext::RemoveRootNodes()
{
	SelectedBones.RemoveAll([this](int32 Index) {
		return FGeometryCollectionClusteringUtility::IsARootBone(this->GeometryCollection.Get(), Index);
		});
}

TMap<int32, TArray<int32>> FFractureToolContext::GetClusteredSelections()
{
	TMap<int32, TArray<int32>> SiblingGroups;

	// Bin the selection indices by parent index
	const TManagedArray<int32>& Parents = GeometryCollection->GetAttribute<int32>("Parent", FGeometryCollection::TransformGroup);
	for (int32 Index : SelectedBones)
	{
		TArray<int32>& SiblingIndices = SiblingGroups.FindOrAdd(Parents[Index]);
		SiblingIndices.Add(Index);
	}

	return SiblingGroups;
}

void FFractureToolContext::ConvertSelectionToLeafNodes()
{
	Sanitize();

	TArray<int32> LeafSelection;
	for (int32 Index : SelectedBones)
	{
		ConvertSelectionToLeafNodes(Index, LeafSelection);
	}

	SelectedBones = LeafSelection;
}

void FFractureToolContext::ConvertSelectionToLeafNodes(int32 Index, TArray<int32>& LeafSelection)
{
	const TManagedArray<TSet<int32>>& Children = GeometryCollection->GetAttribute<TSet<int32>>("Children", FGeometryCollection::TransformGroup);
	if (Children[Index].Num() == 0)
	{
		LeafSelection.Add(Index);
	}
	else 
	{
		for (int32 Child : Children[Index])
		{
			ConvertSelectionToLeafNodes(Child, LeafSelection);
		}
	}	
}


void FFractureToolContext::ConvertSelectionToRigidNodes()
{
	Sanitize();

	TArray<int32> RigidSelection;
	for (int32 Index : SelectedBones)
	{
		ConvertSelectionToRigidNodes(Index, RigidSelection);
	}

	SelectedBones = RigidSelection;
}

void FFractureToolContext::ConvertSelectionToRigidNodes(int32 Index, TArray<int32>& RigidSelection)
{
	const TManagedArray<TSet<int32>>& Children = GeometryCollection->Children;
	const TManagedArray<int32>& SimulationType = GeometryCollection->SimulationType;

	if (SimulationType[Index] == FGeometryCollection::ESimulationTypes::FST_Rigid)
	{
		RigidSelection.Add(Index);
	}
	else
	{
		for (int32 Child : Children[Index])
		{
			ConvertSelectionToRigidNodes(Child, RigidSelection);
		}
	}
}

void FFractureToolContext::ConvertSelectionToEmbeddedGeometryNodes()
{
	Sanitize();

	TArray<int32> EmbeddedSelection;
	for (int32 Index : SelectedBones)
	{
		ConvertSelectionToEmbeddedGeometryNodes(Index, EmbeddedSelection);
	}

	SelectedBones = EmbeddedSelection;
}

void FFractureToolContext::ConvertSelectionToEmbeddedGeometryNodes(int32 Index, TArray<int32>& EmbeddedSelection)
{
	const TManagedArray<TSet<int32>>& Children = GeometryCollection->Children;
	const TManagedArray<int32>& SimulationType = GeometryCollection->SimulationType;

	if (SimulationType[Index] == FGeometryCollection::ESimulationTypes::FST_None)
	{
		EmbeddedSelection.Add(Index);
	}
	else
	{
		for (int32 Child : Children[Index])
		{
			ConvertSelectionToEmbeddedGeometryNodes(Child, EmbeddedSelection);
		}
	}
}

void FFractureToolContext::ConvertSelectionToClusterNodes()
{
	// If this is a non-cluster node, select the cluster containing it instead.
	const TManagedArray<TSet<int32>>& Children = GeometryCollection->Children;
	const TManagedArray<int32>& Parents = GeometryCollection->Parent;
	const TManagedArray<int32>& SimulationType = GeometryCollection->SimulationType;

	TArray<int32> AddedClusterSelections;
	for (int32 Index : SelectedBones)
	{
		if (SimulationType[Index] == FGeometryCollection::ESimulationTypes::FST_Rigid)
		{
			AddedClusterSelections.AddUnique(Parents[Index]);
		}
		else if (SimulationType[Index] == FGeometryCollection::ESimulationTypes::FST_None)
		{
			AddedClusterSelections.AddUnique(Parents[Parents[Index]]);
		}
	}
	SelectedBones.Append(AddedClusterSelections);
	
	Sanitize();
}

void FFractureToolContext::TransformBoundsToWorld()
{
	Bounds = Bounds.TransformBy(Transform);
}


bool FFractureToolContext::HasSelectedAncestor(int32 Index) const
{
	const TManagedArray<int32>& Parents = GeometryCollection->GetAttribute<int32>("Parent", FGeometryCollection::TransformGroup);

	int32 CurrIndex = Index;
	while (Parents[CurrIndex] != INDEX_NONE)
	{
		CurrIndex = Parents[CurrIndex];
		if (SelectedBones.Contains(CurrIndex))
		{
			return true;
		}
	}

	// We've arrived at the top of the hierarchy with no selected ancestors
	return false;
}

