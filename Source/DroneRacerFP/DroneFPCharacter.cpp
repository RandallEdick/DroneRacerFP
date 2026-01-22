#include "DroneFPCharacter.h"
#include "DjiHidReader.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"

// Alias the channel struct from the HID reader so we can write FDjiChannels
using FDjiChannels = FDjiHidReader::FDjiChannels;

// ===== FPV-style rate calculation (Betaflight-inspired) =====
static float ApplyFpvRates(
	float stick,      // -1..1
	float expo,       // 0..1
	float rcRate,     // ~0.5..2.0
	float superRate   // 0..1
)
{
	stick = FMath::Clamp(stick, -1.f, 1.f);
	expo = FMath::Clamp(expo, 0.f, 1.f);
	rcRate = FMath::Max(0.f, rcRate);
	superRate = FMath::Clamp(superRate, 0.f, 0.99f);

	// 1) Expo (cubic blend)
	float x = (1.f - expo) * stick + expo * stick * stick * stick;

	return x;

	//// 2) RC Rate (linear scaling)
	//float rate = x * rcRate;

	//// 3) SuperRate (end-stick boost)
	//const float absRate = FMath::Abs(rate);
	//// Clamp absRate to slightly less than 1.0 to prevent division by zero
	//const float ClampedAbsRate = FMath::Min(absRate, 0.99f);

	//if (ClampedAbsRate > KINDA_SMALL_NUMBER)
	//{
	//	// Use the clamped version here
	//	rate = rate * (1.f + superRate * ClampedAbsRate / (1.f - ClampedAbsRate));
	//}
	//return rate; // still normalized (-∞..∞), scaled later
}

static float ApplyCubicExpo(float x, float expo)
{
	x = FMath::Clamp(x, -1.f, 1.f);
	expo = FMath::Clamp(expo, 0.f, 1.f);

	// Blend linear and cubic: (1-e)*x + e*x^3
	return (1.f - expo) * x + expo * x * x * x;
}

ADroneFPCharacter::ADroneFPCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		for (int i = 0; i < 8; i++)
		{
			float v = PC->GetInputAnalogKeyState(
				FKey(*FString::Printf(TEXT("GenericUSBController Axis%d"), i))
			);

			if (FMath::Abs(v) > 0.01f)
			{
				UE_LOG(LogTemp, Warning, TEXT("Axis %d = %f"), i, v);
			}
		}
	}
	GenericHid = CreateDefaultSubobject<UGenericHidInputComponent>(TEXT("GenericHid"));
	GenericHid->bAutoStart = true;
	GenericHid->bLogDevices = true;

	//controller aggregator
	AxisAgg = CreateDefaultSubobject<UControllerAxisAggregatorComponent>(TEXT("AxisAgg"));

	// Resize the collision capsule
	GetCapsuleComponent()->InitCapsuleSize(12.0f, 7.0f);

	// ===== Camera Tilt Pivot =====
	CameraTiltPivot = CreateDefaultSubobject<USceneComponent>(TEXT("CameraTiltPivot"));
	CameraTiltPivot->SetupAttachment(GetCapsuleComponent());
	CameraTiltPivot->SetRelativeLocation(FVector(0.f, 0.f, 64.f)); // camera height

	// ===== First-person camera =====
	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(CameraTiltPivot);
	FirstPersonCamera->bUsePawnControlRotation = false; // we rotate the whole actor
	FirstPersonCamera->bAutoActivate = true;

	// ===== Arms mesh (kept for compatibility, not used visually) =====
	Mesh1P = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CharacterMesh1P"));

	// ===== Character movement component: disabled (custom physics) =====
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (MoveComp)
	{
		MoveComp->SetMovementMode(MOVE_Flying);
		MoveComp->GravityScale = 0.f;
		MoveComp->Velocity = FVector::ZeroVector;
		MoveComp->bOrientRotationToMovement = false;
		MoveComp->bUseControllerDesiredRotation = false;
		MoveComp->Deactivate(); // We drive movement manually
	}

	// ===== We control rotation directly on the actor =====
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	AutoPossessPlayer = EAutoReceiveInput::Player0;
}

void ADroneFPCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Initialize health
	Health = MaxHealth;

#if PLATFORM_WINDOWS
	FDjiHidReader::Get().Start();
#endif

	UE_LOG(LogTemp, Warning, TEXT("ADroneFPCharacter::BeginPlay (%s)"),
		IsLocallyControlled() ? TEXT("Local") : TEXT("Remote"));

	// Hide / disable arms mesh (visual only)
	if (Mesh1P)
	{
		Mesh1P->SetHiddenInGame(true);
		Mesh1P->SetVisibility(false, true);
		Mesh1P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// ---- Local-only setup (camera + input) ----
	if (!GetController() || !GetController()->IsLocalController())
	{
		return;
	}


	// ----- Enhanced Input -----
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (ULocalPlayer* LP = PC->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsys =
				ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP))
			{
				if (IMC_Default)
				{
					Subsys->AddMappingContext(IMC_Default, 0);
					UE_LOG(LogTemp, Warning, TEXT("Added IMC_Default"));
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("IMC_Default is NULL"));
				}
			}
		}
	}

	// ----- Camera inheritance (critical) -----
	if (CameraTiltPivot)
	{
		CameraTiltPivot->SetUsingAbsoluteRotation(false);
		CameraTiltPivot->SetRelativeRotation(
			FRotator(CameraTiltDegrees, 0.f, 0.f)
		);
	}

	if (FirstPersonCamera)
	{
		FirstPersonCamera->SetUsingAbsoluteRotation(false);
		FirstPersonCamera->bUsePawnControlRotation = false;
		FirstPersonCamera->Activate(true);
	}

	// Debug confirmation
	UE_LOG(LogTemp, Warning, TEXT("Camera Tilt Degrees: %.1f"), CameraTiltDegrees);
	UE_LOG(LogTemp, Warning, TEXT("Pivot RelRot: %s"),
		*CameraTiltPivot->GetRelativeRotation().ToString());
	UE_LOG(LogTemp, Warning, TEXT("Camera WorldRot: %s"),
		*FirstPersonCamera->GetComponentRotation().ToString());
}

void ADroneFPCharacter::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	if (FirstPersonCamera)
	{
		const FRotator CamWorld = FirstPersonCamera->GetComponentRotation();
		const FRotator PivotWorld = CameraTiltPivot ? CameraTiltPivot->GetComponentRotation() : FRotator::ZeroRotator;

		UE_LOG(LogTemp, Warning, TEXT("CalcCamera: CamWorld=%s PivotWorld=%s Actor=%s Local=%d"),
			*CamWorld.ToString(),
			*PivotWorld.ToString(),
			*GetActorRotation().ToString(),
			IsLocallyControlled() ? 1 : 0);

		FirstPersonCamera->GetCameraView(DeltaTime, OutResult);
		return;
	}

	Super::CalcCamera(DeltaTime, OutResult);
}

void ADroneFPCharacter::ApplyMappingContext()
{
	APlayerController* PC = Cast<APlayerController>(Controller);
	if (!PC) return;

	ULocalPlayer* LP = PC->GetLocalPlayer();
	if (!LP) return;

	UEnhancedInputLocalPlayerSubsystem* Subsys =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP);

	if (!Subsys) return;

	if (DefaultMappingContext)
	{
		// Optional: clear others first
		Subsys->ClearAllMappings();
		Subsys->AddMappingContext(DefaultMappingContext, /*Priority*/ 0);

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				-1, 5.0f, FColor::Cyan,
				FString::Printf(TEXT("Applied Mapping Context: %s"),
					*DefaultMappingContext->GetName()));
		}
	}
}

void ADroneFPCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UE_LOG(LogTemp, Warning, TEXT("ADroneFPCharacter::SetupPlayerInputComponent called"));
	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (IA_Throttle)
		{
			EIC->BindAction(IA_Throttle, ETriggerEvent::Triggered,
				this, &ADroneFPCharacter::Throttle);
		}

		if (IA_Yaw)
		{
			EIC->BindAction(IA_Yaw, ETriggerEvent::Triggered,
				this, &ADroneFPCharacter::Yaw);
		}

		if (IA_Pitch)
		{
			EIC->BindAction(IA_Pitch, ETriggerEvent::Triggered,
				this, &ADroneFPCharacter::Pitch);
		}

		if (IA_Roll)
		{
			EIC->BindAction(IA_Roll, ETriggerEvent::Triggered,
				this, &ADroneFPCharacter::Roll);
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("PlayerInputComponent is NOT an EnhancedInputComponent!"));
	}
}

void ADroneFPCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (GEngine)
	{
		// 1. Get the Player Controller
		APlayerController* PC = Cast<APlayerController>(GetController());
		if (PC)
		{
			// 2. Scan GenericUSBController Axes 1 through 8
			for (int32 i = 1; i <= 8; i++)
			{
				FKey AxisKey = FKey(FName(*FString::Printf(TEXT("GenericUSBController_Axis%d"), i)));
				float AxisValue = PC->GetInputAnalogKeyState(AxisKey);

				// 3. Only display if the value is not zero (to clear the clutter)
				if (FMath::Abs(AxisValue) > 0.01f)
				{
					// Use Key '10 + i' to ensure each axis gets its own line (Line 11, 12, etc.)
					GEngine->AddOnScreenDebugMessage(10 + i, 0.1f, FColor::Orange,
						FString::Printf(TEXT("DETECTED - Axis %d: %.3f"), i, AxisValue));
				}
			}
		}
	}
	if (DeltaTime <= 0.f)
	{
		return;
	}



// A) Camera tilt
	UpdateCameraTilt();

	// 0) Process commands
	float PitchCmd = 0.f;
	float RollCmd = 0.f;
	float YawCmd = 0.f;

	// IMPORTANT: Check your SmoothInputs function. 
	// It should be using the member variables (PitchInput, etc.) 
	// that your Enhanced Input functions are now updating.
	SmoothInputs(DeltaTime, PitchCmd, RollCmd, YawCmd);

	// 1) Update orientation
	UpdateOrientation(DeltaTime, PitchCmd, RollCmd, YawCmd);

	// 2–3) Compute net acceleration (thrust + gravity + drag)
	const FVector Accel = ComputeTotalAcceleration(DeltaTime);

	// 4) Integrate movement and handle collisions
	IntegrateMovement(DeltaTime, Accel);
}

void ADroneFPCharacter::UpdateCameraTilt()
{
	if (IsLocallyControlled() && CameraTiltPivot)
	{
		CameraTiltPivot->SetUsingAbsoluteRotation(false);
		CameraTiltPivot->SetRelativeRotation(FRotator(CameraTiltDegrees, 0.f, 0.f));
	}
}

void ADroneFPCharacter::SmoothInputs(
	float DeltaTime,
	float& OutPitchCmd,
	float& OutRollCmd,
	float& OutYawCmd)
{
	// Smooth stick inputs
	PitchInputSmoothed = FMath::FInterpTo(PitchInputSmoothed, PitchInput, DeltaTime, RotSmoothSpeed);
	RollInputSmoothed = FMath::FInterpTo(RollInputSmoothed, RollInput, DeltaTime, RotSmoothSpeed);
	YawInputSmoothed = FMath::FInterpTo(YawInputSmoothed, YawInput, DeltaTime, RotSmoothSpeed);

	// Apply FPV expo / rates
	OutPitchCmd = ApplyFpvRates(
		PitchInputSmoothed,
		PitchExpo,
		PitchRcRate,
		PitchSuperRate
	);

	OutRollCmd = ApplyFpvRates(
		RollInputSmoothed,
		RollExpo,
		RollRcRate,
		RollSuperRate
	);

	OutYawCmd = ApplyFpvRates(
		YawInputSmoothed,
		YawExpo,
		YawRcRate,
		YawSuperRate
	);

	UE_LOG(LogTemp, Warning, TEXT("RawPitch=%.3f Smoothed=%.3f Cmd=%.3f"),
		PitchInput,
		PitchInputSmoothed,
		OutPitchCmd);
}

void ADroneFPCharacter::VelocityDebugPrint()
{
	const float SpeedCm = Velocity.Size();
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			1,                      // key (replaces same line)
			0.f,                    // display for this frame only
			FColor::Yellow,
			FString::Printf(
				TEXT("Vel = %s | Speed = %.1f cm/s (%.2f m/s)"),
				*Velocity.ToString(),
				SpeedCm,
				SpeedCm / 100.f
			)
		);
	}
}

void ADroneFPCharacter::UpdateOrientation(
	float DeltaTime,
	float PitchCmd,
	float RollCmd,
	float YawCmd)
{
	// Flip pitch so +stick pitches DOWN (your existing behavior)
	const float dPitch = PitchCmd * PitchRateDeg * DeltaTime;
	const float dYaw = YawCmd * YawRateDeg * DeltaTime;
	const float dRoll = RollCmd * RollRateDeg * DeltaTime;

	AddActorLocalRotation(FRotator(dPitch, dYaw, dRoll), false);
}

FVector ADroneFPCharacter::ComputeTotalAcceleration(float DeltaTime)
{
	const float SafeMass = FMath::Max(Mass, KINDA_SMALL_NUMBER);

	// --- Thrust (only when armed) ---
	FVector ThrustAccel = FVector::ZeroVector;
	if (bThrottleArmed)
	{
		ThrustAccel = ComputeThrustAccel(DeltaTime);  // your function
	}
	else
	{
		// Optionally: relax throttle when disarmed
		ThrottleSmoothed = FMath::FInterpTo(ThrottleSmoothed, 0.f, DeltaTime, ThrustResponse);
	}

	// --- Gravity (as acceleration) ---
	const float GravityZ = GetWorld() ? GetWorld()->GetGravityZ() : -980.f; // cm/s^2
	const FVector GravityAccel(0.f, 0.f, GravityZ);

	// --- Drag ---
	const FVector DragForce = -DragCoeff * Velocity;
	const FVector DragAccel = DragForce / SafeMass;

	// Total acceleration
	return ThrustAccel + GravityAccel + DragAccel;
}

void ADroneFPCharacter::IntegrateMovement(float DeltaTime, const FVector& Accel)
{
	// Integrate velocity
	Velocity += Accel * DeltaTime;

	// Integrate position
	const FVector Delta = Velocity * DeltaTime;

	FHitResult Hit;
	AddActorWorldOffset(Delta, /*bSweep=*/true, &Hit);

	if (!Hit.IsValidBlockingHit())
	{
		return;
	}

	// Prefer ImpactNormal for the surface normal
	const FVector SurfaceNormal = Hit.ImpactNormal.IsNearlyZero()
		? Hit.Normal.GetSafeNormal()
		: Hit.ImpactNormal.GetSafeNormal();

	// Remove into-surface velocity (prevents tunneling / pogo)
	const float Vn = FVector::DotProduct(Velocity, SurfaceNormal);
	if (Vn < 0.f)
	{
		Velocity -= SurfaceNormal * Vn;
	}

	// Ground-ish behavior: don't "launch" from tiny penetrations
	if (SurfaceNormal.Z > 0.6f)
	{
		// Apply friction to lateral velocity only (so you can still lift)
		FVector Lateral(Velocity.X, Velocity.Y, 0.f);
		constexpr float GroundFriction = 3.f; // tune 0..8
		Lateral = FMath::VInterpTo(Lateral, FVector::ZeroVector, DeltaTime, GroundFriction);
		Velocity.X = Lateral.X;
		Velocity.Y = Lateral.Y;
	}

	HandleImpactDamage(Hit);
}

void ADroneFPCharacter::HandleImpactDamage(const FHitResult& Hit)
{
	if (!Hit.IsValidBlockingHit()) return;
	if (Health <= 0.f) return;
	if (Hit.GetActor() && Hit.GetActor()->GetName().Contains(TEXT("FoliageActor")))
	{
		bool HitFoliage = true;
	}
	const FVector Normal = Hit.Normal.GetSafeNormal();

	// Velocity is in cm/s. We want component INTO the surface.
	const float Vn = FVector::DotProduct(Velocity, Normal);
	const float ImpactSpeedCm = FMath::Max(-Vn, 0.f); // only if moving into the surface
	// Convert to m/s for energy calculation
	const float ImpactSpeedM = ImpactSpeedCm / 100.f;

	// Kinetic energy-ish: 0.5 * m * v^2
	const float ImpactEnergy = 0.5f * Mass * ImpactSpeedM * ImpactSpeedM;

	// Hardness multiplier based on what we hit (1.0 = neutral)
	const float Hardness = GetSurfaceHardness(Hit);

	if (ImpactSpeedCm <= KINDA_SMALL_NUMBER)
	{
		// Always log (so we can see why Damage might be 0)
		UE_LOG(LogTemp, Warning,
			TEXT("IMPACT DEBUG Grazing | Mass= %.3f | Normal=%s | Vel=%s | Vn=%.3f | ImpactSpeed=%.3f cm/s (%.8f m/s) | Energy=%.6f | Hardness=%.3f "),
			Mass,
			*Normal.ToString(),
			*Velocity.ToString(),
			Vn,
			ImpactSpeedCm,
			ImpactSpeedM,
			ImpactEnergy,
			Hardness
		);

		UE_LOG(LogTemp, Warning,
			TEXT("Grazing Impact"));
		return; // grazing / sliding, no real impact
	}

	// Map energy range to 0..1 damage factor
	const float Damage01 = FMath::GetMappedRangeValueClamped(
		FVector2D(MinEnergyForDamage, MaxEnergyForMaxDamage),
		FVector2D(0.f, 1.f),
		ImpactEnergy
	);

	const float Damage = Damage01 * MaxDamagePerImpact * Hardness;

	// Always log (so we can see why Damage might be 0)
	UE_LOG(LogTemp, Warning,
		TEXT("IMPACT DEBUG DirectHit | Mass= %.3f | Normal=%s | Vel=%s | Vn=%.3f | ImpactSpeed=%.3f cm/s (%.6f m/s) | Energy=%.8f | Hardness=%.3f | Damage01=%.3f | Damage=%.3f | Health=%.1f/%.1f"),
		Mass,
		*Normal.ToString(),
		*Velocity.ToString(),
		Vn,
		ImpactSpeedCm,
		ImpactSpeedM,
		ImpactEnergy,
		Hardness,
		Damage01,
		Damage,
		Health, MaxHealth
	);

	if (Damage > 0.f)
	{
		ApplyDamageToDrone(Damage);

		UE_LOG(LogTemp, Warning,
			TEXT("Impact: Speed=%.1f cm/s (%.2f m/s), Energy=%.2f, Hardness=%.2f, Damage=%.2f, Health=%.1f/%.1f"),
			ImpactSpeedCm, ImpactSpeedM, ImpactEnergy, Hardness, Damage, Health, MaxHealth);
	}
}

void DebugHit(const FHitResult& Hit)
{
	UE_LOG(LogTemp, Warning,
		TEXT("HIT DEBUG\n Actor=%s\n Comp=%s\n Class=%s\n Instance=%d\n ImpactPoint=%s\n ImpactNormal=%s"),
		*GetNameSafe(Hit.GetActor()),
		*GetNameSafe(Hit.GetComponent()),
		Hit.GetActor() ? *Hit.GetActor()->GetClass()->GetName() : TEXT("None"),
		Hit.Item,
		*Hit.ImpactPoint.ToString(),
		*Hit.ImpactNormal.ToString()
	);
}

FVector ADroneFPCharacter::ComputeThrustAccel(float DeltaTime)
{
	// 1) Smooth throttle (unchanged)
	ThrottleSmoothed = FMath::FInterpTo(
		ThrottleSmoothed,
		Throttle01,
		DeltaTime,
		ThrustResponse
	);

	// 2) Map throttle around hover into t in [-1, +1]
	float t = 0.f;

	if (ThrottleSmoothed >= HoverThrottle)
	{
		// scale [HoverThrottle..1] -> [0..1]
		t = (ThrottleSmoothed - HoverThrottle) /
			FMath::Max(1e-3f, (1.f - HoverThrottle));
	}
	else
	{
		// scale [0..HoverThrottle] -> [-1..0]
		t = (ThrottleSmoothed - HoverThrottle) /
			FMath::Max(1e-3f, HoverThrottle);
	}

	// 3) Expo shaping around hover
	const float Sign = (t >= 0.f) ? 1.f : -1.f;
	const float Mag = FMath::Pow(FMath::Abs(t), ThrustExpo);
	const float Shaped = Sign * Mag;   // in [-1 .. +1]

	// 4) Convert shaped value into an UPWARD acceleration

	// Gravity is negative in UE (e.g. -980 cm/s^2)
	const float GravityZ = GetWorld()->GetGravityZ();
	const float HoverAccel = -GravityZ;                 // +g  (about 980 cm/s^2)

	const float MaxUpAccel = HoverAccel * MaxThrustG;   // e.g. 2g at full throttle
	const float MinUpAccel = 0.f;                       // no thrust

	float UpAccel = HoverAccel; // default at shaped=0 (hover)

	if (Shaped >= 0.f)
	{
		// Shaped in [0..1] → lerp from HoverAccel to MaxUpAccel
		UpAccel = FMath::Lerp(HoverAccel, MaxUpAccel, Shaped);
	}
	else
	{
		// Shaped in [-1..0] → lerp from MinUpAccel to HoverAccel
		// remap [-1..0] -> [0..1]
		const float Alpha = Shaped + 1.f;  // -1→0, 0→1
		UpAccel = FMath::Lerp(MinUpAccel, HoverAccel, Alpha);
	}
	const float NetZ = UpAccel + GravityZ;          // thrust + gravity
	// Read current vertical velocity (if you track it):
	float VelocityZ = 0.f;
	if (Velocity.IsNearlyZero() == false)
	{
		VelocityZ = Velocity.Z;
	}
	float Altitude = GetActorLocation().Z;

	UE_LOG(LogTemp, Warning,
		TEXT("THRUST DEBUG | Thr=%.3f Sm=%.3f Hover=%.3f Shaped=%.3f | Up=%.1f Grav=%.1f Net=%.1f | VelZ=%.1f AltZ=%.1f"),
		Throttle01,
		ThrottleSmoothed,
		HoverThrottle,
		Shaped,
		UpAccel,
		GravityZ,
		NetZ,
		VelocityZ,
		Altitude
	);
	// 5) Return thrust acceleration along the drone's Up axis
	return GetActorUpVector() * UpAccel;
}

float ADroneFPCharacter::GetSurfaceHardness(const FHitResult& Hit) const
{
	// Default if nothing special
	float Hardness = 1.0f;

	if (UPhysicalMaterial* PhysMat = Hit.PhysMaterial.Get())
	{
		EPhysicalSurface SurfaceType = UPhysicalMaterial::DetermineSurfaceType(PhysMat);

		// You can customize these in Project Settings -> Physics -> Physical Surfaces
		switch (SurfaceType)
		{
		case SurfaceType1: // FleshDefault
			Hardness = 0.3f;
			break;

		case SurfaceType2: // Wood
			Hardness = 0.7f;
			break;

		case SurfaceType3: // Metal
		case SurfaceType4: // Concrete
			Hardness = 1.5f;
			break;

		default:
			Hardness = 1.0f;
			break;
		}
	}

	return Hardness;
}

void ADroneFPCharacter::ApplyDamageToDrone(float DamageAmount)
{
	if (DamageAmount <= 0.f || Health <= 0.f) return;

	Health = FMath::Clamp(Health - DamageAmount, 0.f, MaxHealth);

	if (Health <= 0.f)
	{
		OnDroneDestroyed();
	}
}

void ADroneFPCharacter::OnDroneDestroyed()
{
	UE_LOG(LogTemp, Warning, TEXT("Drone destroyed!"));

	// Simple behavior: disarm and stop
	bThrottleArmed = false;
	Velocity = FVector::ZeroVector;

	// You could also:
	// - Enable SimulatePhysics on mesh and let it ragdoll
	// - Trigger explosion FX
	// - Restart level, etc.
}

// ===== Input handlers: store latest stick values =====

void ADroneFPCharacter::Throttle(const FInputActionValue& Value)
{
	float RawValue = Value.Get<float>();

	// Arming logic: Requires stick at bottom (-.05) to start motors
	if (!bThrottleArmed && RawValue <= -0.05)
	{
		bThrottleArmed = true;
		UE_LOG(LogTemp, Warning, TEXT("Drone Armed via Enhanced Input!"));
	}
	bThrottleArmed = true;
	if (bThrottleArmed)
	{
		float RawInput = Value.Get<float>();

		// This takes your input (which is now roughly 0.0 to 1.0 thanks to the Scalar)
		// and forces it to stay strictly within 0.0 and 1.0
		ThrottleInput = FMath::Clamp(RawInput, 0.0f, 1.0f);

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(1, 0.1f, FColor::Yellow,
				FString::Printf(TEXT("Throttle: %.3f"), ThrottleInput));
		}
	}
}

void ADroneFPCharacter::Yaw(const FInputActionValue& Value)
{
	YawInput = Value.Get<float>();
	UE_LOG(LogTemp, Warning, TEXT("YawInput = %.3f"), YawInput);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			2,                 // A unique Key (use -1 to stack messages, or a fixed number to overwrite)
			0.1f,              // Time to display (keep short for real-time values)
			FColor::Green,      // Text Color
			FString::Printf(TEXT("Yaw Input: %.3f"), YawInput) // The formatted float
		);
	}
}

void ADroneFPCharacter::Pitch(const FInputActionValue& Value)
{
	// Standard Sim: Forward stick = Nose Down. 
	// If your IMC doesn't have a 'Negate' modifier, do it here:
	PitchInput = -Value.Get<float>();
	UE_LOG(LogTemp, Warning, TEXT("PitchInput = %.3f"), PitchInput);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			3,                 // A unique Key (use -1 to stack messages, or a fixed number to overwrite)
			0.1f,              // Time to display (keep short for real-time values)
			FColor::Cyan,      // Text Color
			FString::Printf(TEXT("Pitch Input: %.3f"), PitchInput) // The formatted float
		);
	}
}

void ADroneFPCharacter::Roll(const FInputActionValue& Value)
{
	RollInput = Value.Get<float>();
	UE_LOG(LogTemp, Warning, TEXT("RollInput = %.3f"), RollInput);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			4,                 // A unique Key (use -1 to stack messages, or a fixed number to overwrite)
			0.1f,              // Time to display (keep short for real-time values)
			FColor::Magenta,      // Text Color
			FString::Printf(TEXT("Roll Input: %.3f"), RollInput) // The formatted float
		);
	}
}

static float Deadzone1D(float v, float dz = 0.1f)
{
	return (FMath::Abs(v) < dz) ? 0.f : v;
}

void ADroneFPCharacter::Move(const FInputActionValue& Value)
{
	const FVector2D Input = Value.Get<FVector2D>();
	const float X = Deadzone1D(Input.X);
	const float Y = Deadzone1D(Input.Y);

	if (Controller && (FMath::Abs(X) > 0.f || FMath::Abs(Y) > 0.f))
	{
		const FRotator YawRot(0.f, Controller->GetControlRotation().Yaw, 0.f);
		const FVector Forward = FRotationMatrix(YawRot).GetUnitAxis(EAxis::X);
		const FVector Right = FRotationMatrix(YawRot).GetUnitAxis(EAxis::Y);

		AddMovementInput(Forward, Y);
		AddMovementInput(Right, X);
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			-1, 0.f, FColor::Green,
			FString::Printf(TEXT("Move: X=%.2f Y=%.2f"), X, Y));
	}
}

void ADroneFPCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D Input = Value.Get<FVector2D>();
	const float X = Deadzone1D(Input.X);
	const float Y = Deadzone1D(Input.Y);

	AddControllerYawInput(X);
	AddControllerPitchInput(Y);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			-1, 0.f, FColor::Cyan,
			FString::Printf(TEXT("Look: X=%.2f Y=%.2f"), X, Y));
	}
}

void ADroneFPCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
#if PLATFORM_WINDOWS
	FDjiHidReader::Get().Stop();
#endif

	Super::EndPlay(EndPlayReason);
}
void ADroneFPCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	TrySetupEnhancedInput();
}

void ADroneFPCharacter::OnRep_Controller()
{
	Super::OnRep_Controller();
	TrySetupEnhancedInput();
}

void ADroneFPCharacter::TrySetupEnhancedInput()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->IsLocalController())
	{
		UE_LOG(LogTemp, Warning, TEXT("TrySetupEnhancedInput: no local PC yet"));
		return;
	}

	ULocalPlayer* LP = PC->GetLocalPlayer();
	if (!LP)
	{
		UE_LOG(LogTemp, Warning, TEXT("TrySetupEnhancedInput: no LocalPlayer"));
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* Subsys = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!Subsys)
	{
		UE_LOG(LogTemp, Warning, TEXT("TrySetupEnhancedInput: no EnhancedInput subsystem"));
		return;
	}

	if (!IMC_Default)
	{
		UE_LOG(LogTemp, Error, TEXT("TrySetupEnhancedInput: IMC_Default is NULL"));
		return;
	}

	Subsys->AddMappingContext(IMC_Default, 0);
	UE_LOG(LogTemp, Warning, TEXT("TrySetupEnhancedInput: Added IMC_Default"));
}
