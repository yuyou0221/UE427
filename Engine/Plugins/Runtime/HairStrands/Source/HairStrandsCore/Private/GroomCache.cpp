// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomCache.h"
#include "GroomAsset.h"

void UGroomCache::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	int32 NumChunks = Chunks.Num();
	Ar << NumChunks;

	if (Ar.IsLoading())
	{
		Chunks.SetNum(NumChunks);
	}

	for (int32 ChunkId = 0; ChunkId < NumChunks; ++ChunkId)
	{
		Chunks[ChunkId].Serialize(Ar, this, ChunkId);
	}
}

void UGroomCache::Initialize(EGroomCacheType Type)
{
	GroomCacheInfo.Type = Type;
}

int32 UGroomCache::GetStartFrame() const
{
	return GroomCacheInfo.AnimationInfo.StartFrame;
}

int32 UGroomCache::GetEndFrame() const
{
	return GroomCacheInfo.AnimationInfo.EndFrame;
}

float UGroomCache::GetDuration() const
{
	return GroomCacheInfo.AnimationInfo.Duration;
}

int32 UGroomCache::GetFrameNumberAtTime(const float Time, bool bLooping) const
{
	return GetStartFrame() + GetFrameIndexAtTime(Time, bLooping); 
}

int32 UGroomCache::GetFrameIndexAtTime(const float Time, bool bLooping) const
{
	const float FrameTime = GroomCacheInfo.AnimationInfo.SecondsPerFrame;
	if (FrameTime == 0.0f)
	{
		return 0;
	}

	const int32 NumFrames = GroomCacheInfo.AnimationInfo.NumFrames;
	const float Duration = GroomCacheInfo.AnimationInfo.Duration;
	float AdjustedTime = Time;
	if (bLooping)
	{
		AdjustedTime = Time - Duration * FMath::FloorToFloat(Time / Duration);
	}
	else
	{
		AdjustedTime = FMath::Clamp(Time, 0.0f, GroomCacheInfo.AnimationInfo.EndTime - GroomCacheInfo.AnimationInfo.StartTime);
	}

	const int32 NormalizedFrame = FMath::Clamp(FMath::FloorToInt(AdjustedTime / FrameTime), 0, NumFrames - 1);
	return NormalizedFrame; 
}

void UGroomCache::FindSampleIndexesFromTime(float Time, bool bLooping, bool bIsPlayingBackwards, int32 &OutFrameIndex, int32 &OutNextFrameIndex, float &InterpolationFactor)
{
	const int32 NumFrames = GroomCacheInfo.AnimationInfo.NumFrames;
	const float Duration = GroomCacheInfo.AnimationInfo.Duration;

	// No index possible
	if (NumFrames == 0 || NumFrames == 1 || Duration == 0.0f)
	{
		OutFrameIndex = 0;
		OutNextFrameIndex = 0;
		InterpolationFactor = 0.0f;
		return;
	}

	OutFrameIndex = GetFrameIndexAtTime(Time, bLooping);
	OutNextFrameIndex = FMath::Min(OutFrameIndex + 1, NumFrames - 1);

	const float FrameDuration = GroomCacheInfo.AnimationInfo.SecondsPerFrame;
	if (FMath::IsNearlyZero(FrameDuration))
	{
		InterpolationFactor = 0.0f;
	}
	else
	{
		float AdjustedTime;

		if (bLooping)
		{
			AdjustedTime = Time - Duration * FMath::FloorToFloat(Time / Duration);
		}
		else
		{
			AdjustedTime = FMath::Clamp(Time, 0.0f, GroomCacheInfo.AnimationInfo.EndTime - GroomCacheInfo.AnimationInfo.StartTime);
		}

		float Delta = AdjustedTime - FrameDuration * OutFrameIndex;
		InterpolationFactor = Delta / FrameDuration;
	}

	// If playing backwards the logical order of previous and next is reversed
	if (bIsPlayingBackwards)
	{
		Swap(OutFrameIndex, OutNextFrameIndex);
		InterpolationFactor = 1.0f - InterpolationFactor;
	}
}

bool UGroomCache::GetGroomDataAtTime(float Time, bool bLooping, FGroomCacheAnimationData& AnimData)
{
	const int32 FrameIndex = GetFrameIndexAtTime(Time, bLooping);
	return GetGroomDataAtFrameIndex(FrameIndex, AnimData);
}

bool UGroomCache::GetGroomDataAtFrameIndex(int32 FrameIndex, FGroomCacheAnimationData& AnimData)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UGroomCache::GetGroomDataAtFrameIndex);

	if (!Chunks.IsValidIndex(FrameIndex))
	{
		return false;
	}

	// This is the reverse operation of how the GroomCacheAnimationData is processed into a GroomCacheChunk
	FGroomCacheChunk& Chunk = Chunks[FrameIndex];

	TArray<uint8> TempBytes;
	TempBytes.SetNum(Chunk.DataSize);

	// This is where the bulk data is loaded from disk
	void* Buffer = TempBytes.GetData();
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UGroomCache::GetGroomDataAtFrameIndex_BulkData);
		Chunk.BulkData.GetCopy(&Buffer, true);
	}

	// The bulk data buffer is then serialized into GroomCacheAnimationData
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UGroomCache::GetGroomDataAtFrameIndex_Serialize);
		FMemoryReader Ar(TempBytes, true);
		AnimData.Serialize(Ar);
	}

	return true;
}

void UGroomCache::SetGroomAnimationInfo(const FGroomAnimationInfo& AnimInfo)
{
	GroomCacheInfo.AnimationInfo = AnimInfo;

	// Ensure that the guides groom cache serialize only positions
	if (GroomCacheInfo.Type == EGroomCacheType::Guides)
	{
		GroomCacheInfo.AnimationInfo.Attributes &= EGroomCacheAttributes::Position;
	}
}

EGroomCacheType UGroomCache::GetType() const
{
	return GroomCacheInfo.Type;
}

void FGroomCacheChunk::Serialize(FArchive& Ar, UObject* Owner, int32 ChunkIndex)
{
	Ar << DataSize;
	Ar << FrameIndex;

	// Forced not inline means the bulk data won't automatically be loaded when we deserialize
	// but only when we explicitly take action to load it
	BulkData.SetBulkDataFlags(BULKDATA_Force_NOT_InlinePayload);
	BulkData.Serialize(Ar, Owner, ChunkIndex, false);
}

FGroomCacheProcessor::FGroomCacheProcessor(EGroomCacheType InType, EGroomCacheAttributes InAttributes)
: Attributes(InAttributes)
, Type(InType)
{
}

void FGroomCacheProcessor::AddGroomSample(TArray<FHairGroupData>&& GroupData)
{
	TArray<uint8> TempBytes;
	FMemoryWriter Ar(TempBytes, true);

	// The HairGroupData is converted into GroomCacheAnimationData and serialized to a buffer
	FGroomCacheAnimationData AnimData(MoveTemp(GroupData), FGroomCacheInfo::GetCurrentVersion(), Type, Attributes);
	AnimData.Serialize(Ar);

	FGroomCacheChunk& Chunk = Chunks.AddDefaulted_GetRef();

	// The buffer is then stored into bulk data
	const int32 DataSize = TempBytes.Num();
	Chunk.DataSize = DataSize;
	Chunk.FrameIndex = Chunks.Num() - 1;
	Chunk.BulkData.Lock(LOCK_READ_WRITE);
	void* ChunkBuffer = Chunk.BulkData.Realloc(DataSize);
	FMemory::Memcpy(ChunkBuffer, TempBytes.GetData(), DataSize);
	Chunk.BulkData.Unlock();
}

void FGroomCacheProcessor::TransferChunks(UGroomCache* GroomCache)
{
	GroomCache->Chunks = MoveTemp(Chunks);
}
