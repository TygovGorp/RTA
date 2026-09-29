#include "Raytraced_Audio.h"
#include "Log.h"
#include "RTADecayMetrics.h"
#include "HAL/IConsoleManager.h"

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

	IModularFeatures::Get().RegisterModularFeature(
		IAudioOcclusionFactory::GetModularFeatureName(), &OcclusionFactory);
	IModularFeatures::Get().RegisterModularFeature(
		IAudioReverbFactory::GetModularFeatureName(), &ReverbFactory);

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
		
		auto FDN = FFeedbackDelayNetwork();
		FDN.Init(48000.f);
		FDN.SetRT60(RT60);
		FDN.SetDamping(0);
		FDN.Reset();
		
		auto NumSamples = ceil(RT60 * 2.f * 48000.f);
		
		TArray<float> Capture;
		Capture.SetNumUninitialized(NumSamples);
		Capture[0] = FDN.ProcessSample(1.0f);

		for (int n = 1; n < NumSamples; ++n)
			Capture[n] = FDN.ProcessSample(0.0f);
		
		auto Echo = FEchogram();
		
		for (int n = 0; n < NumSamples; ++n)
		{
			auto Bin = floor((n / 48000.f) / FEchogram::BinWidthSeconds);
			if (Bin < FEchogram::NumBins)
				Echo.At(0, Bin) += Capture[n] * Capture[n];
		}
		
		auto M = RTA::ComputeDecayMetrics(Echo, 0);
		UE_LOG(LogRTA, Log, TEXT("asked %.2f  measured T30 %.2f  |R| %.3f  curvature %.1f%%  window ok %d"), RT60, M.T30, abs(M.T30_R), M.CurvaturePercent, M.bWindowSufficient);
	   	
	   	FString Csv = TEXT("Sample,Time,Amplitude\n");
		for (int32 n = 0; n < NumSamples; ++n)
		{
			Csv += FString::Printf(TEXT("%d,%.6f,%.8f\n"), n, n / 48000.f, Capture[n]);
		}
		const FString Path = FPaths::ProjectSavedDir() / TEXT("RTA_FDN_IR.csv");
		FFileHelper::SaveStringToFile(Csv, *Path);
		UE_LOG(LogRTA, Log, TEXT("FDN IR written to %s"), *Path);
	}));
}

void FRaytracedAudioModule::ShutdownModule()
{
	DumpEchogramCommand.Reset();
	TestFDNCommand.Reset();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRaytracedAudioModule, Raytraced_Audio)