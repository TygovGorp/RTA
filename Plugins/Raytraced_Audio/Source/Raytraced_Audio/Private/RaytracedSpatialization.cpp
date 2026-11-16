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
	
	using namespace RTA::HeadModel;

	enum class EEar { Left, Right };

	struct FShadowCoeffs { float B0, B1, A1; };

	float LateralAngle(const FVector3f& Dir)
	{
		if (Dir.IsNearlyZero()) return 0.f;
		return FMath::Asin(FMath::Clamp(Dir.Y, -1.f, 1.f));
	}

	float WoodworthITD(float Theta)
	{
		const float T = FMath::Abs(Theta);
		return (HeadRadius / SpeedOfSound) * (T + FMath::Sin(T));
	}

	float ReadDelayed(const float* Buffer, int32 WritePos, float Delay)
	{
		const float ReadPos = WritePos - Delay;
		const int32 I = FMath::FloorToInt32(ReadPos);
		const float Frac = ReadPos - I;
		const float Older = Buffer[I & (DelaySize - 1)];          // negative I wraps correctly
		const float Newer = Buffer[(I + 1) & (DelaySize - 1)];
		return Older + Frac * (Newer - Older);
	}

	float ShadowAlpha(float Theta, EEar Ear)
	{
		const float S = FMath::Sin(Theta);
		return FMath::Max(Ear == EEar::Left ? 1.f - S : 1.f + S, AlphaMin);
	}

	FShadowCoeffs ShadowCoefficients(float Alpha, float SampleRate)
	{
		const float Beta = 2.f * SpeedOfSound / HeadRadius;
		const float K = 2.f * SampleRate;
		return { (Beta + Alpha * K) / (Beta + K),
				 (Beta - Alpha * K) / (Beta + K),
				 (Beta - K)         / (Beta + K) };
	}

	float ShadowProcess(FShadowFilterState& S, float X, const FShadowCoeffs& C)
	{
		const float Y = C.B0 * X + C.B1 * S.X1 - C.A1 * S.Y1;
		S.X1 = X;
		S.Y1 = Y;
		return Y;
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
	SampleRate   = InitializationParams.SampleRate;
	BufferLength = InitializationParams.BufferLength;
	NumSources   = InitializationParams.NumSources;

	HeadModelStates.SetNum(NumSources);

	if (!HRIRSet.Load(TEXT("C:/Code/Git/RTA/Plugins/Raytraced_Audio/Content/Demo/General/Audio/Spatialization/HRTF/riec_080.bin")))
	{
		UE_LOG(LogRTA, Warning, TEXT("Spatialization: no HRTF loaded, HRTF mode unavailable"));
	}

	bInitialized = true;
	UE_LOG(LogRTA, Log, TEXT("Spatialization initialised"));
}

bool FRaytracedSpatialization::CreateSpatializationEffect(uint32 SourceId)
{
	return true;
}


void FRaytracedSpatialization::OnInitSource(const uint32 SourceId, const FName& AudioComponentUserId,
	const uint32 NumChannels, USpatializationPluginSourceSettingsBase* InSettings)
{
	if (HeadModelStates.IsValidIndex(SourceId))
	{
		HeadModelStates[SourceId] = RTA::HeadModel::FState(); // no leftover tail from the previous sound
	}
}

void FRaytracedSpatialization::OnReleaseSource(const uint32 SourceId)
{
	//TODO
}

void FRaytracedSpatialization::ProcessAudio(const FAudioPluginSourceInputData& InputData,
	FAudioPluginSourceOutputData& OutputData)
{
	const float* In = InputData.AudioBuffer->GetData();
	float* Out = OutputData.AudioBuffer.GetData();
	const int32 NumFrames = InputData.AudioBuffer->Num(); // mono in: one sample per frame

	if (OutputData.AudioBuffer.Num() != 2 * NumFrames || !HeadModelStates.IsValidIndex(InputData.SourceId))
	{
		// Unexpected layout or unknown source: pass through rather than write out of bounds.
		const int32 Frames = FMath::Min(NumFrames, OutputData.AudioBuffer.Num() / 2);
		for (int32 n = 0; n < Frames; ++n)
		{
			Out[2 * n] = Out[2 * n + 1] = In[n];
		}
		return;
	}

	const FVector3f Dir = FVector3f(InputData.SpatializationParams->EmitterPosition).GetSafeNormal();
	ProcessHeadModel(HeadModelStates[InputData.SourceId], In, Out, NumFrames, Dir);
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

void FRaytracedSpatialization::ProcessHeadModel(RTA::HeadModel::FState& State, const float* In,
	float* OutInterleaved, int32 NumFrames, const FVector3f& Dir) const
{
	const float Theta = LateralAngle(Dir);
	const float ItdSamples = WoodworthITD(Theta) * SampleRate;
	const float TargetDelayL = Theta > 0.f ? ItdSamples : 0.f;   // source right → left ear is far
	const float TargetDelayR = Theta > 0.f ? 0.f : ItdSamples;

	const FShadowCoeffs CoeffsL = ShadowCoefficients(ShadowAlpha(Theta, EEar::Left),  SampleRate);
	const FShadowCoeffs CoeffsR = ShadowCoefficients(ShadowAlpha(Theta, EEar::Right), SampleRate);

	for (int32 n = 0; n < NumFrames; ++n)
	{
		State.WritePos = (State.WritePos + 1) & (DelaySize - 1);
		State.Buffer[State.WritePos] = In[n];

		const float T = float(n + 1) / float(NumFrames);       // ramp delays across the block
		const float DelayL = FMath::Lerp(State.PrevDelayL, TargetDelayL, T);
		const float DelayR = FMath::Lerp(State.PrevDelayR, TargetDelayR, T);

		const float Left  = ReadDelayed(State.Buffer, State.WritePos, DelayL);
		const float Right = ReadDelayed(State.Buffer, State.WritePos, DelayR);

		OutInterleaved[2 * n]     = ShadowProcess(State.ShadowL, Left,  CoeffsL) * HeadModelGain;
		OutInterleaved[2 * n + 1] = ShadowProcess(State.ShadowR, Right, CoeffsR) * HeadModelGain;
	}

	State.PrevDelayL = TargetDelayL;
	State.PrevDelayR = TargetDelayR;
}
