// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DroneRacerFP : ModuleRules
{
    public DroneRacerFP(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "InputCore",
                "EnhancedInput",
                "PhysicsCore",
                "UMG"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
            }
        );

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicSystemLibraries.AddRange(
                new string[]
                {
                    "hid.lib",
                    "setupapi.lib"
                }
            );
        }
    }
}

