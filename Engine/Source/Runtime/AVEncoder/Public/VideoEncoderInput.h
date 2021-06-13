// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VideoCommon.h"

#if WITH_CUDA
#include "CudaModule.h"
#endif

#if PLATFORM_WINDOWS
struct ID3D11Device;
struct ID3D11Texture2D;
struct ID3D12Device;
struct ID3D12Resource;
#endif

// vulkan forward declaration
struct VkImage_T;
struct VkDevice_T;

namespace AVEncoder
{
	class FVideoEncoderInputFrame;

	class AVENCODER_API FVideoEncoderInput
	{
	public:
		// --- construct video encoder input based on expected input frame format
		static TSharedPtr<FVideoEncoderInput> CreateDummy(uint32 InWidth, uint32 InHeight, bool isResizable = false);
		static TSharedPtr<FVideoEncoderInput> CreateForYUV420P(uint32 InWidth, uint32 InHeight, bool isResizable = false);

		// create input for an encoder that encodes a D3D11 texture
		static TSharedPtr<FVideoEncoderInput> CreateForD3D11(void* InApplicationD3D11Device, uint32 InWidth, uint32 InHeight, bool isResizable = false);

		// TODO (M84FIX) AMF can work with this but also can handle raw D3D12 textures we should add support for that too 
		// create input for an encoder that encodes a D3D12 texture in the context of a D3D11 device (i.e. nvenc)
		static TSharedPtr<FVideoEncoderInput> CreateForD3D12(void* InApplicationD3D12Device, uint32 InWidth, uint32 InHeight, bool isResizable = false);

		// create input for an encoder that encodes a CUarray in the context of a CUcontext (i.e. nvenc)
		static TSharedPtr<FVideoEncoderInput> CreateForCUDA(void* InApplicationCudaContext, uint32 InWidth, uint32 InHeight, bool isResizable = false);

		// create input for an encoder that encodes a VkImage in the context of a VkDevice (i.e. Amf)
		static TSharedPtr<FVideoEncoderInput> CreateForVulkan(void* InApplicationVulkanDevice, uint32 InWidth, uint32 InHeight, bool isResizable = false);

		// --- properties
		virtual void SetResolution(uint32 InWidth, uint32 InHeight);

		EVideoFrameFormat GetFrameFormat() const { return FrameFormat; }

		// --- available encoders

		// get a list of supported video encoders
		virtual const TArray<FVideoEncoderInfo>& GetAvailableEncoders() = 0;

		// --- create encoders

		// --- encoder input frames - user managed

		// new packet callback prototype void(uint32 LayerIndex, const FCodecPacket& Packet)
		using OnFrameReleasedCallback = TFunction<void(const FVideoEncoderInputFrame* /* InReleasedFrame */)>;

		// create a user managed buffer
		virtual FVideoEncoderInputFrame* CreateBuffer(OnFrameReleasedCallback InOnFrameReleased) = 0;

		// destroy user managed buffer
		virtual void DestroyBuffer(FVideoEncoderInputFrame* Buffer) = 0;

		// --- encoder input frames - managed by this object

		// obtain a video frame that can be used as a buffer for input to a video encoder
		virtual FVideoEncoderInputFrame* ObtainInputFrame() = 0;

		// release (free) an input frame and make it available for future use
		virtual void ReleaseInputFrame(FVideoEncoderInputFrame* InFrame) = 0;

		// destroy/release any frames that are not currently in use
		virtual void Flush() = 0;

	protected:
		FVideoEncoderInput() = default;
		virtual ~FVideoEncoderInput() = default;
		FVideoEncoderInput(const FVideoEncoderInput&) = delete;
		FVideoEncoderInput& operator=(const FVideoEncoderInput&) = delete;

		EVideoFrameFormat				FrameFormat = EVideoFrameFormat::Undefined;
		uint32 Width;
		uint32 Height;

		bool bIsResizable = false;
	};


	// TODO this should go elsewhere and be made cross platform
	class AVENCODER_API FVideoEncoderInputFrame
	{
	public:
		// Obtain (increase reference count) of this input frame
		const FVideoEncoderInputFrame* Obtain() const { NumReferences.Increment(); return this; }
		// Release (decrease reference count) of this input frame
		virtual void Release() const = 0;

		// the callback type used to create a registered encoder
		using FCloneDestroyedCallback = TFunction<void(const FVideoEncoderInputFrame* /* InCloneAboutToBeDestroyed */)>;

		// Clone frame - this will create a copy that references the original until destroyed
		virtual const FVideoEncoderInputFrame* Clone(FCloneDestroyedCallback InCloneDestroyedCallback) const = 0;

		void SetFrameID(uint32 id) { FrameID = id; }
		uint32 GetFrameID() const { return FrameID; }

		void SetTimestampUs(int64 timestampUs) { TimestampUs = timestampUs; }
		int64 GetTimestampUs() const { return TimestampUs; }

		void SetTimestampRTP(int64 timestampRTP) { TimestampRTP = timestampRTP; }
		int64 GetTimestampRTP() const { return TimestampRTP; }

		// current format of frame
		EVideoFrameFormat GetFormat() const { return Format; }
		// width of frame buffer
		uint32 GetWidth() const { return Width; }
		// height of frame buffer
		uint32 GetHeight() const { return Height; }

		TFunction<void()> OnTextureEncode;

		// --- YUV420P

		struct FYUV420P
		{
			const uint8*		Data[3] = { nullptr, nullptr, nullptr };
			uint32				StrideY = 0;
			uint32				StrideU = 0;
			uint32				StrideV = 0;
		};

		void AllocateYUV420P();
		const FYUV420P& GetYUV420P() const 
		{	
			return YUV420P; 
		}
		
		FYUV420P& GetYUV420P() { return YUV420P; }

		void SetYUV420P(const uint8* InDataY, const uint8* InDataU, const uint8* InDataV, uint32 InStrideY, uint32 InStrideU, uint32 InStrideV);

#if PLATFORM_WINDOWS
		// --- D3D11

		struct FD3D11
		{
			ID3D11Texture2D*	Texture = nullptr;
			ID3D11Device*		EncoderDevice = nullptr;
			ID3D11Texture2D*	EncoderTexture = nullptr;
			void*				SharedHandle = nullptr;
		};

		const FD3D11& GetD3D11() const { return D3D11; }
		FD3D11& GetD3D11() { return D3D11; }

		// the callback type used to create a registered encoder
		using FReleaseD3D11TextureCallback = TFunction<void(ID3D11Texture2D*)>;

		void SetTexture(ID3D11Texture2D* InTexture, FReleaseD3D11TextureCallback InOnReleaseD3D11Texture);

		// --- D3D12

		struct FD3D12
		{
			ID3D12Resource*		Texture = nullptr;
			ID3D12Device*		EncoderDevice = nullptr;
			ID3D12Resource*		EncoderTexture = nullptr;
		};

		const FD3D12& GetD3D12() const { return D3D12; }
		FD3D12& GetD3D12() { return D3D12; }

		// the callback type used to create a registered encoder
		using FReleaseD3D12TextureCallback = TFunction<void(ID3D12Resource*)>;

		void SetTexture(ID3D12Resource* InTexture, FReleaseD3D12TextureCallback InOnReleaseD3D11Texture);

#endif // PLATFORM_WINDOWS

#if WITH_CUDA

		// --- CUDA
		struct FCUDA
		{
			CUarray		EncoderTexture;
			CUcontext   EncoderDevice;
		};

		const FCUDA& GetCUDA() const { return CUDA; }
		FCUDA& GetCUDA() { return CUDA; }

		// the callback type used to create a registered encoder
		using FReleaseCUDATextureCallback = TFunction<void(CUarray)>;

		void SetTexture(CUarray InTexture, FReleaseCUDATextureCallback InOnReleaseTexture);

#endif // WITH_CUDA

		// --- Vulkan
		struct FVulkan
		{
			VkImage_T*		EncoderTexture;
			VkDevice_T*		EncoderDevice;
		};

		const FVulkan& GetVulkan() const { return Vulkan; }
		FVulkan& GetVulkan() { return Vulkan; }

		// the callback type used to create a registered encoder
		using FReleaseVulkanTextureCallback = TFunction<void(VkImage_T*)>;

#if PLATFORM_WINDOWS || PLATFORM_LINUX
		void SetTexture(VkImage_T* InTexture, FReleaseVulkanTextureCallback InOnReleaseTexture);
#endif

	protected:
		FVideoEncoderInputFrame();
		explicit FVideoEncoderInputFrame(const FVideoEncoderInputFrame& CloneFrom);
		virtual ~FVideoEncoderInputFrame();

		uint32									FrameID;
		int64									TimestampUs;
		int64									TimestampRTP;
		mutable FThreadSafeCounter				NumReferences;
		EVideoFrameFormat						Format;
		uint32									Width;
		uint32									Height;
		FYUV420P								YUV420P;
		bool									bFreeYUV420PData;

#if PLATFORM_WINDOWS
		FD3D11									D3D11;
		FReleaseD3D11TextureCallback			OnReleaseD3D11Texture;
		FD3D12									D3D12;
		FReleaseD3D12TextureCallback			OnReleaseD3D12Texture;
#endif

#if WITH_CUDA
		FCUDA									CUDA;
		FReleaseCUDATextureCallback				OnReleaseCUDATexture;
#endif

		FVulkan									Vulkan;
		FReleaseVulkanTextureCallback			OnReleaseVulkanTexture;
	};


} /* namespace AVEncoder */
