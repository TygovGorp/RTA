#include "RaytracedSpatialization.h"
#include "Log.h"
#include "Misc/FileHelper.h"
#include "Serialization/MemoryReader.h"

namespace
{
	constexpr int64 HeaderBytes = 3 * sizeof(int32);

	/** SOFA uses X forward, Y left, Z up; Unreal uses X forward, Y right, Z up, so Y is negated. */
	FVector3f SofaToUnrealDirection(float AzimuthDegrees, float ElevationDegrees)
	{
		const float Az = FMath::DegreesToRadians(AzimuthDegrees);
		const float El = FMath::DegreesToRadians(ElevationDegrees);
		const float CosEl = FMath::Cos(El);
		return FVector3f(CosEl * FMath::Cos(Az), -CosEl * FMath::Sin(Az), FMath::Sin(El));
	}
}

void FRTAHRIRSet::Reset()
{
	Measurements.Empty();
	SampleRate = 0;
	Length = 0;
}

bool FRTAHRIRSet::Load(const FString& Path)
{
	Reset();

	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		UE_LOG(LogRTA, Error, TEXT("HRIR: could not read %s"), *Path);
		return false;
	}

	
	if (Bytes.Num() < HeaderBytes)
	{
		UE_LOG(LogRTA, Error, TEXT("HRIR: %s is too small to contain a header (%d bytes)"), *Path, Bytes.Num());
		return false;
	}

	FMemoryReader Reader(Bytes);
	int32 NumDirections = 0;
	int32 FileLength = 0;
	int32 FileSampleRate = 0;
	Reader << NumDirections << FileLength << FileSampleRate;

	if (NumDirections <= 0 || FileLength <= 0 || FileSampleRate <= 0)
	{
		UE_LOG(LogRTA, Error, TEXT("HRIR: invalid header in %s (directions %d, length %d, sample rate %d)"),
			*Path, NumDirections, FileLength, FileSampleRate);
		return false;
	}
	
	// Per direction: azimuth + elevation, then left and right samples.
	const int64 BytesPerDirection = 2 * sizeof(float) + 2 * int64(FileLength) * sizeof(float);
	const int64 ExpectedBytes = HeaderBytes + int64(NumDirections) * BytesPerDirection;
	if (Bytes.Num() != ExpectedBytes)
	{
		UE_LOG(LogRTA, Error, TEXT("HRIR: %s is %d bytes, expected %lld for %d directions of %d samples"),
			*Path, Bytes.Num(), ExpectedBytes, NumDirections, FileLength);
		return false;
	}

	Measurements.SetNum(NumDirections);
	for (FHRIRMeasurement& M : Measurements)
	{
		Reader << M.Azimuth << M.Elevation;
		M.Direction = SofaToUnrealDirection(M.Azimuth, M.Elevation);

		M.LeftSamples.SetNumUninitialized(FileLength);
		M.RightSamples.SetNumUninitialized(FileLength);
		Reader.Serialize(M.LeftSamples.GetData(), FileLength * sizeof(float));
		Reader.Serialize(M.RightSamples.GetData(), FileLength * sizeof(float));
	}

	if (Reader.IsError())
	{
		UE_LOG(LogRTA, Error, TEXT("HRIR: read error in %s"), *Path);
		Reset();
		return false;
	}

	SampleRate = FileSampleRate;
	Length = FileLength;

	UE_LOG(LogRTA, Log, TEXT("HRIR: loaded %s (%d directions, %d samples, %d Hz)"),
		*Path, NumDirections, Length, SampleRate);
	return true;
}

void FRaytracedSpatialization::Initialize(const FAudioPluginInitializationParams InitializationParams)
{
	//TODO
	SampleRate =InitializationParams.SampleRate;
	BufferLength = InitializationParams.BufferLength;
	NumSources = InitializationParams.NumSources;
	
	PerSourceStates.Reserve(NumSources);
	
	HRIRSet.Load(TEXT("C:/Code/Git/RTA/Plugins/Raytraced_Audio/Content/Demo/General/Audio/Spatialization/HRTF/riec_080.bin"));
	
	UE_LOG(LogRTA, Log, TEXT("Spatialization initialised"));
	bInitialized = true;
}

bool FRaytracedSpatialization::CreateSpatializationEffect(uint32 SourceId)
{
	return true;
}


void FRaytracedSpatialization::OnInitSource(const uint32 SourceId, const FName& AudioComponentUserId,
	const uint32 NumChannels, USpatializationPluginSourceSettingsBase* InSettings)

{
	//TODO
	UE_LOG(LogRTA, Log, TEXT("Spatialization New Source Initialised"));
}

void FRaytracedSpatialization::OnReleaseSource(const uint32 SourceId)
{
	//TODO
}

void FRaytracedSpatialization::ProcessAudio(const FAudioPluginSourceInputData& InputData,
	FAudioPluginSourceOutputData& OutputData)
{
	auto EmmitterDir = InputData.SpatializationParams->EmitterPosition;
	EmmitterDir.Normalize();
	
	const auto Rot = EmmitterDir.Rotation();
	
	const auto AzimuthDeg= Rot.Yaw;
	const auto ElevationDeg = Rot.Pitch;
	
	OutputData.AudioBuffer = *InputData.AudioBuffer;
}

void FRaytracedSpatialization::OnAllSourcesProcessed()
{
	//TODO
}

void FRaytracedSpatialization::Shutdown()
{
	//TODO
}

void FRaytracedSpatialization::OnDeviceShutdown(FAudioDevice* AudioDevice)
{
	//TODO
}


void* FRaytracedSpatialization::GetSpatializationEffect(uint32 SourceId)
{
	return nullptr;
}

bool FRaytracedSpatialization::IsSpatializationEffectInitialized() const
{
	return bInitialized;
}
