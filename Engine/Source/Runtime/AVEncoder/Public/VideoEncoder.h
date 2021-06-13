// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VideoEncoderInput.h"
#include "Misc/FrameRate.h"
#include "Misc/ScopeLock.h"

namespace AVEncoder
{
    class AVENCODER_API FVideoEncoder
    {
    public:
        enum class RateControlMode { UNKNOWN, CONSTQP, VBR, CBR };
        enum class MultipassMode { UNKNOWN, DISABLED, QUARTER, FULL };

        struct FLayerConfig
        {
            uint32			Width = 0;
            uint32			Height = 0;
            uint32			MaxFramerate = 0;
            int32			MaxBitrate = 0;
            int32			TargetBitrate = 0;
            int32			QPMax = -1;
            int32			QPMin = -1;
            RateControlMode RateControlMode = RateControlMode::CBR;
            MultipassMode	MultipassMode = MultipassMode::FULL;
            bool			FillData = false;

			bool operator==(FLayerConfig const& other) const
			{
				return Width == other.Width
					&& Height == other.Height
					&& MaxFramerate == other.MaxFramerate
					&& MaxBitrate == other.MaxBitrate
					&& TargetBitrate == other.TargetBitrate
					&& QPMax == other.QPMax
					&& QPMin == other.QPMin
					&& RateControlMode == other.RateControlMode
					&& MultipassMode == other.MultipassMode
					&& FillData == other.FillData;
			}

			bool operator!=(FLayerConfig const& other) const
			{
				return !(*this == other);
			}
        };

        virtual ~FVideoEncoder();

		virtual bool Setup(TSharedRef<FVideoEncoderInput> input, FLayerConfig const& config) { return false; }
		virtual void Shutdown() {}

        virtual bool AddLayer(FLayerConfig const& config);
        uint32 GetNumLayers() const { return static_cast<uint32>(Layers.Num()); }
        virtual uint32 GetMaxLayers() const { return 1; }

		FLayerConfig GetLayerConfig(uint32 layerIdx) const;
        void UpdateLayerConfig(uint32 layerIdx, FLayerConfig const& config);

        using OnFrameEncodedCallback = TFunction<void(const FVideoEncoderInputFrame* /* InCompletedFrame */)>;
        using OnEncodedPacketCallback = TFunction<void(uint32 /* LayerIndex */, const FVideoEncoderInputFrame* /* Frame */, const FCodecPacket& /* Packet */)>;

        struct FEncodeOptions
        {
            bool					bForceKeyFrame = false;
            OnFrameEncodedCallback	OnFrameEncoded;
        };

        void SetOnEncodedPacket(OnEncodedPacketCallback callback) { OnEncodedPacket = MoveTemp(callback); }
        void ClearOnEncodedPacket() { OnEncodedPacket = nullptr; }

		virtual void Encode(FVideoEncoderInputFrame const* frame, FEncodeOptions const& options) {}

    protected:
        FVideoEncoder() = default;

        class FLayer
        {
        public:
            explicit FLayer(FLayerConfig const& layerConfig)
                : CurrentConfig(layerConfig)
                , NeedsReconfigure(false)
            {}
            virtual ~FLayer() = default;

			FLayerConfig const& GetConfig() const { return CurrentConfig; }
			void UpdateConfig(FLayerConfig const& config)
			{
				FScopeLock lock(&ConfigMutex);
				CurrentConfig = config;
				NeedsReconfigure = true;
			}

		protected:
			FCriticalSection	ConfigMutex;
            FLayerConfig		CurrentConfig;
            bool				NeedsReconfigure;
        };

		virtual FLayer* CreateLayer(uint32 layerIdx, FLayerConfig const& config) { return nullptr; }
		virtual void DestroyLayer(FLayer* layer) {};

        TArray<FLayer*>			Layers;
        OnEncodedPacketCallback	OnEncodedPacket;
    };
}