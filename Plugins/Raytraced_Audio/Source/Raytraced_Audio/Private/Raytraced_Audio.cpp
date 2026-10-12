#include "Raytraced_Audio.h"
#include "Log.h"
#include "RTADecayMetrics.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

DEFINE_LOG_CATEGORY(LogRTA);

#define LOCTEXT_NAMESPACE "FRaytraced_AudioModule"

void FRaytracedAudioModule::StartupModule()
{
	UE_LOG(LogRTA, Log, TEXT("Started"));

	AudioPluginListener = MakeShared<FAudioPluginListener>();
	AudioPluginListener->SetRTManagerMapPtr(&RTManagerMap);

	OcclusionFactory.SetRTManagerMapPtr(&RTManagerMap);
	OcclusionFactory.SetAudioPluginListenerPtr(AudioPluginListener);

	ReverbFactory.SetRTManagerMapPtr(&RTManagerMap);
	ReverbFactory.SetAudioPluginListenerPtr(AudioPluginListener);

	SourceDataOverrideFactory.SetRTManagerMapPtr(&RTManagerMap);

	IModularFeatures::Get().RegisterModularFeature(
		IAudioOcclusionFactory::GetModularFeatureName(), &OcclusionFactory);
	IModularFeatures::Get().RegisterModularFeature(
		IAudioReverbFactory::GetModularFeatureName(), &ReverbFactory);
	IModularFeatures::Get().RegisterModularFeature(
		IAudioSourceDataOverrideFactory::GetModularFeatureName(), &SourceDataOverrideFactory);

	DumpEchogramCommand = MakeUnique<FAutoConsoleCommand>(
		TEXT("rta.DumpEchogram"),
		TEXT("Writes the accumulated echogram to Saved/RTA_Echogram_<timestamp>.xlsx"),
		FConsoleCommandDelegate::CreateLambda([this]()
		{
			if (RTManagerMap.Num() == 0)
			{
				UE_LOG(LogRTA, Warning, TEXT("rta.DumpEchogram: no active raytrace manager."));
				return;
			}
			for (const TTuple<FAudioDevice*, TSharedPtr<FRaytraceManager>>& Entry : RTManagerMap)
			{
				if (Entry.Value.IsValid())
				{
					Entry.Value->DumpEchogram();
				}
			}
		}));
	TestFDNCommand = MakeUnique<FAutoConsoleCommand>(
	TEXT("rta.TestFDN"),
	   TEXT("Measures the FDN's own decay against a requested RT60. Usage: rta.TestFDN [seconds]"),
	   FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const float RT60 = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 2.0f;
		constexpr float SampleRate = 48000.f;

		FFeedbackDelayNetwork FDN;
		FDN.Init(SampleRate);
		FDN.SetRT60(RT60);
		FDN.SetDamping(0);
		FDN.Reset();

		const int32 NumSamples = FMath::CeilToInt32(RT60 * 2.f * SampleRate);
		if (NumSamples <= 0)
		{
			UE_LOG(LogRTA, Warning, TEXT("rta.TestFDN: RT60 must be positive"));
			return;
		}

		// Impulse response, both channels.
		TArray<float> L, R;
		L.SetNumUninitialized(NumSamples);
		R.SetNumUninitialized(NumSamples);
		for (int32 n = 0; n < NumSamples; ++n)
		{
			const TStaticArray<float, 2> Out = FDN.ProcessSample(n == 0 ? 1.f : 0.f);
			L[n] = Out[0];
			R[n] = Out[1];
		}

	   	const float IRSeconds = NumSamples / SampleRate;
	   	const int32 Stretch = FMath::Max(1, FMath::CeilToInt32(IRSeconds / FEchogram::MaxTimeSeconds));
		const float EffectiveBinSeconds = FEchogram::BinWidthSeconds * Stretch;

		FEchogram Echo;
		for (int32 n = 0; n < NumSamples; ++n)
		{
			const int32 Bin = FMath::FloorToInt32((n / SampleRate) / EffectiveBinSeconds);
			if (Bin >= FEchogram::NumBins) break;
			Echo.At(0, Bin) += L[n] * L[n] + R[n] * R[n];
			Echo.At(1, Bin) += L[n] * L[n];
			Echo.At(2, Bin) += R[n] * R[n];
		}
		FDecayMetric Both  = RTA::ComputeDecayMetrics(Echo, 0);
		FDecayMetric Left  = RTA::ComputeDecayMetrics(Echo, 1);
		FDecayMetric Right = RTA::ComputeDecayMetrics(Echo, 2);
		for (FDecayMetric* M : { &Both, &Left, &Right })
		{
			M->EDT *= Stretch;
			M->T20 *= Stretch;
			M->T30 *= Stretch;
		}

		auto Correlate = [&L, &R, NumSamples](int32 Begin, int32 End, double& OutRho, double& OutIacc)
		{
			Begin = FMath::Clamp(Begin, 0, NumSamples);
			End = FMath::Clamp(End, Begin, NumSamples);

			double EnergyL = 0.0, EnergyR = 0.0;
			for (int32 n = Begin; n < End; ++n)
			{
				EnergyL += double(L[n]) * L[n];
				EnergyR += double(R[n]) * R[n];
			}

			OutRho = 0.0;
			OutIacc = 0.0;
			const double Norm = FMath::Sqrt(EnergyL * EnergyR);
			if (Norm <= 0.0) return;

			const int32 MaxLag = FMath::RoundToInt32(0.001f * SampleRate);
			for (int32 Lag = -MaxLag; Lag <= MaxLag; ++Lag)
			{
				double Cross = 0.0;
				for (int32 n = Begin; n < End; ++n)
				{
					const int32 m = n + Lag;
					if (m >= Begin && m < End)
					{
						Cross += double(L[n]) * R[m];
					}
				}
				const double Rho = Cross / Norm;
				if (Lag == 0) OutRho = Rho;
				OutIacc = FMath::Max(OutIacc, FMath::Abs(Rho));
			}
		};

		const int32 Split = FMath::Min(FMath::RoundToInt32(0.080f * SampleRate), NumSamples);
		double RhoEarly, IaccEarly, RhoLate, IaccLate;
		Correlate(0, Split, RhoEarly, IaccEarly);
		Correlate(Split, NumSamples, RhoLate, IaccLate);

		UE_LOG(LogRTA, Log, TEXT("rta.TestFDN asked %.2f s  T30 both %.2f  left %.2f  right %.2f  |R| %.3f  curvature %.1f%%  window ok %d"),
			RT60, Both.T30, Left.T30, Right.T30, FMath::Abs(Both.T30_R), Both.CurvaturePercent, Both.bWindowSufficient ? 1 : 0);
		UE_LOG(LogRTA, Log, TEXT("rta.TestFDN L/R  early (0-80 ms): rho %+.3f  IACC %.3f   late (80 ms+): rho %+.3f  IACC %.3f"),
			RhoEarly, IaccEarly, RhoLate, IaccLate);

		FString Csv = TEXT("Sample,Time,Left,Right\n");
		Csv.Reserve(NumSamples * 40);
		for (int32 n = 0; n < NumSamples; ++n)
		{
			Csv += FString::Printf(TEXT("%d,%.6f,%.8f,%.8f\n"), n, n / SampleRate, L[n], R[n]);
		}
		const FString Path = FPaths::ProjectSavedDir() / TEXT("RTA_FDN_IR.csv");
		FFileHelper::SaveStringToFile(Csv, *Path);
		UE_LOG(LogRTA, Log, TEXT("FDN IR written to %s"), *Path);
	}));
}

void FRaytracedAudioModule::ShutdownModule()
{
	IModularFeatures::Get().UnregisterModularFeature(
		IAudioOcclusionFactory::GetModularFeatureName(), &OcclusionFactory);
	IModularFeatures::Get().UnregisterModularFeature(
		IAudioReverbFactory::GetModularFeatureName(), &ReverbFactory);
	IModularFeatures::Get().UnregisterModularFeature(
		IAudioSourceDataOverrideFactory::GetModularFeatureName(), &SourceDataOverrideFactory);

	DumpEchogramCommand.Reset();
	TestFDNCommand.Reset();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRaytracedAudioModule, Raytraced_Audio)