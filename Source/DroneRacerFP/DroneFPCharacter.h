#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "Components/SkeletalMeshComponent.h"
#include "ControllerAxisAggregatorComponent.h"
#include "DroneFPCharacter.generated.h"

class UCameraComponent;
class UInputAction;
class USceneComponent;
class USkeletalMeshComponent;

/**
 * Physics-based first-person drone character, DJI Mode 2 controls.
 *
 * Left Stick:
 *   Y: Throttle (up/down)
 *   X: Yaw (rotate around vertical axis)
 *
 * Right Stick:
 *   Y: Pitch (tilt nose up/down)
 *   X: Roll (bank left/right, rotation about longitudinal axis)
 */
UCLASS()
class DRONERACERFP_API ADroneFPCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    ADroneFPCharacter();

    virtual void Tick(float DeltaTime) override;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
    virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;

    /** First person camera */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UCameraComponent* FirstPersonCamera;

    // ===== Enhanced Input Actions (set in BP_DroneFPCharacter) =====
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    UInputMappingContext* IMC_Default;

    /** Left Stick Y: Throttle */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    UInputAction* IA_Throttle;

    /** Left Stick X: Yaw */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    UInputAction* IA_Yaw;

    /** Right Stick Y: Pitch */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    UInputAction* IA_Pitch;

    /** Right Stick X: Roll */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    UInputAction* IA_Roll;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    UInputMappingContext* DefaultMappingContext;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    UInputAction* IA_Move;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    UInputAction* IA_Look;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|FPV")
    float CameraTiltDegrees = 20.0f;   // 0–40 typical FPV range;

    UPROPERTY(VisibleAnywhere, Category = "Camera")
    USceneComponent* CameraTiltPivot;

  

    // ===== Raw input state (normalized stick values) =====

    /** Left Stick Y (-1..+1) */
    float ThrottleInput = 0.f;

    /** Left Stick X (-1..+1) */
    float YawInput = 0.f;

    /** Right Stick Y (-1..+1) */
    float PitchInput = 0.f;

    /** Right Stick X (-1..+1) */
    float RollInput = 0.f;

    // ===== Physical parameters =====
    //SMOOTHING OF INPUTS
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Input")
    float ThrottleSmoothSpeed = 5.f; // how quickly we follow stick



    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Input")
    float RotSmoothSpeed = 8.f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Flight|Input")
    float PitchInputSmoothed = 0.f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Flight|Input")
    float RollInputSmoothed = 0.f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Flight|Input")
    float YawInputSmoothed = 0.f;

    /** Drone mass in kg */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight|Physics")
    float Mass = .7f;


    // Thrust/hover tuning
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Thrust")
    float MaxThrustAccel = 2500.f;   // cm/s^2 at full throttle (tune 1500-6000)

    // Full stick up gives this many "g's" upward.
// 2.0f means full throttle = 2g up (nice DJI-ish feeling).
    UPROPERTY(EditAnywhere, Category = "Flight|Thrust")
    float MaxThrustG = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Thrust")
    float HoverThrottle = 0.5f;     // throttle01 value that roughly hovers

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Thrust")
    float ThrustExpo = 0.7f;         // >1 gives finer control near hover

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Thrust")
    float ThrustResponse = 1.0f;     // smoothing rate (bigger = snappier)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Rates")
    float PitchExpo = 0.7f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Rates")
    float RollExpo = 0.7f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Rates")
    float YawExpo = 0.7f;

    // Base sensitivity
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Rates")
    float PitchRcRate = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Rates")
    float RollRcRate = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Rates")
    float YawRcRate = 1.0;

    // End-of-stick acceleration
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Rates")
    float PitchSuperRate = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Rates")
    float RollSuperRate = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Rates")
    float YawSuperRate = 1.0f;

    // Smoothed throttle used by physics
    float ThrottleSmoothed = 0.f;


    /** Linear drag coefficient */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight|Physics")
    float DragCoeff = 1.0f;

    /** Pitch rate (deg/s) at full stick */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight|Physics")
    float PitchRateDeg =360.0f;

    /** Roll rate (deg/s) at full stick */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight|Physics")
    float RollRateDeg = 360.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    UControllerAxisAggregatorComponent* AxisAgg;

    /** Yaw rate (deg/s) at full stick */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight|Physics")
    float YawRateDeg = 180.0f;

    /** Current world-space velocity of the drone (cm/s) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Flight|Physics")
    FVector Velocity = FVector::ZeroVector;

    // Health / damage
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Health")
    float MaxHealth = 100.f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Flight|Health")
    float Health = 100.f;

    // Max damage a *single* impact can do (before hardness multiplier)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Health")
    float MaxDamagePerImpact = 50.f;

    // Minimum & maximum impact energy for mapping to [0..1] damage
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Health")
    float MinEnergyForDamage = 5.f;      // J-ish
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Health")
    float MaxEnergyForMaxDamage = 100.f; // J-ish

    // ===== Input handlers (Enhanced Input) =====

    void Throttle(const FInputActionValue& Value);
    void Yaw(const FInputActionValue& Value);
    void Pitch(const FInputActionValue& Value);
    void Roll(const FInputActionValue& Value);

    void Move(const FInputActionValue& Value);
    void Look(const FInputActionValue& Value);

    void HandleImpactDamage(const FHitResult& Hit);
    float GetSurfaceHardness(const FHitResult& Hit) const;
    void ApplyDamageToDrone(float DamageAmount);
    void OnDroneDestroyed();

private:
    void ApplyMappingContext();
    float Throttle01=0.f;
    bool bThrottleArmed = false;
    FVector ComputeThrustAccel(float DeltaTime);
    float PrevVelocity = 0.f;
    void UpdateCameraTilt();
    void SmoothInputs(float DeltaTime, float& OutPitchCmd, float& OutRollCmd, float& OutYawCmd);
    void UpdateOrientation(float DeltaTime, float PitchCmd, float RollCmd, float YawCmd);
    FVector ComputeTotalAcceleration(float DeltaTime);
    void IntegrateMovement(float DeltaTime, const FVector& Accel);
    void DebugHit(const FHitResult& Hit);
    void VelocityDebugPrint();
    UPROPERTY(VisibleDefaultsOnly, Category = Mesh)
    USkeletalMeshComponent* Mesh1P;
};
