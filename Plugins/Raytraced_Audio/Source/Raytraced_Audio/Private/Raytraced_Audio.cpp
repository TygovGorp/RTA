#include "Raytraced_Audio.h"
#include "Log.h"
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
		TEXT("Writes the accumulated echogram to Saved/RTA_Echogram.csv"),
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
					Entry.Value->DumpEchogramCsv();
				}
			}
		}));
}

void FRaytracedAudioModule::ShutdownModule()
{
	DumpEchogramCommand.Reset();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRaytracedAudioModule, Raytraced_Audio)
