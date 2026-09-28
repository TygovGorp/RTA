using UnrealBuildTool;

public class Raytraced_AudioEditor : ModuleRules
{
	public Raytraced_AudioEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"AudioExtensions",
			"Raytraced_Audio"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"AssetTools",
			"AssetDefinition",
			"Slate",
			"SlateCore"
		});
	}
}
