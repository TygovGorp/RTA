// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Raytraced_AudioEditor : ModuleRules
{
	public Raytraced_AudioEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"AudioExtensions",
				"Raytraced_Audio" // gives us UMyOcclusionSourceSettings
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"UnrealEd",     // UFactory lives here
				"AssetTools",   // asset registration / categories
				"Slate",
				"SlateCore"
			}
		);
	}
}
