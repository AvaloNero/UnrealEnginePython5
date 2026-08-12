#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Subsystems/WorldSubsystem.h"

#include "UEPLyraWorldSubsystem.generated.h"

class ULyraExperienceDefinition;
class ULyraExperienceManagerComponent;
class AGameStateBase;

USTRUCT(BlueprintType)
struct UEPLYRABRIDGE_API FUEPLyraGameFeatureState
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    FString Name;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    FString State;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    bool bActive = false;
};

USTRUCT(BlueprintType)
struct UEPLYRABRIDGE_API FUEPLyraRuntimeSnapshot
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    bool bIsGameThread = false;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    bool bIsGameWorld = false;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    FString WorldName;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    FString NetMode;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    bool bHasServerAuthority = false;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    int32 PlayerControllerCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    int32 LocalPlayerControllerCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    int32 RemotePlayerControllerCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    int32 PlayerStateCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    bool bHasExperienceManager = false;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    bool bExperienceLoaded = false;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    TObjectPtr<UObject> CurrentExperience = nullptr;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    bool bHasPlayerPawn = false;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    bool bPawnLocallyControlled = false;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    bool bPlayerStateReady = false;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    FString PawnLocalRole;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    FString PawnRemoteRole;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    bool bHeroInputReady = false;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    bool bAbilitySystemReady = false;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    bool bHealthReady = false;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    bool bDamageImmune = false;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    float Health = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    float MaxHealth = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    TArray<FUEPLyraGameFeatureState> GameFeatures;
};

/** Machine-readable outcome for the narrow, authority-only 0.5 gameplay slice. */
USTRUCT(BlueprintType)
struct UEPLYRABRIDGE_API FUEPLyraGameplayCommandResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra|Gameplay")
    FString CommandId;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra|Gameplay")
    FString Status;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra|Gameplay")
    bool bAccepted = false;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra|Gameplay")
    bool bApplied = false;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra|Gameplay")
    bool bServerAuthority = false;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra|Gameplay")
    FString TargetActor;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra|Gameplay")
    float RequestedHealthDelta = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra|Gameplay")
    float HealthBefore = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra|Gameplay")
    float HealthAfter = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra|Gameplay")
    float MaxHealth = 0.0f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUEPLyraExperienceReady, UObject*, Experience);

/**
 * Per-world adapter for Python. Most of the surface is observational. The
 * health-delta command is an intentionally narrow 0.5 vertical slice: it can
 * only submit a bounded, non-lethal Lyra GameplayEffect from server authority.
 */
UCLASS()
class UEPLYRABRIDGE_API UUEPLyraWorldSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Deinitialize() override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;

    UFUNCTION(BlueprintPure, Category = "UEP|Lyra")
    FUEPLyraRuntimeSnapshot CaptureSnapshot() const;

    /** Applies one bounded Lyra GAS delta when exactly one controller matches the requested role. */
    UFUNCTION(BlueprintCallable, Category = "UEP|Lyra|Gameplay")
    FUEPLyraGameplayCommandResult ApplyAuthorityHealthDelta(
        const FString& CommandId,
        float HealthDelta,
        bool bTargetRemotePlayer);

    UFUNCTION(BlueprintCallable, Category = "UEP|Lyra")
    void ClearPythonListeners();

    UPROPERTY(BlueprintAssignable, Category = "UEP|Lyra")
    FUEPLyraExperienceReady OnExperienceReady;

protected:
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
    void AttachExperienceManager(AGameStateBase* GameState = nullptr);
    void HandleGameStateSet(AGameStateBase* GameState);
    void HandleExperienceLoaded(const ULyraExperienceDefinition* Experience);

    TWeakObjectPtr<ULyraExperienceManagerComponent> ExperienceManager;
    TWeakObjectPtr<UObject> CurrentExperience;
    bool bExperienceCallbackRegistered = false;
    TSet<FString> ConsumedGameplayCommandIds;
};

/** Static lookup keeps Python independent of Unreal's subsystem templates. */
UCLASS()
class UEPLYRABRIDGE_API UUEPLyraBridgeLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintPure, Category = "UEP|Lyra", meta = (WorldContext = "WorldContextObject"))
    static UUEPLyraWorldSubsystem* GetBridgeForWorld(const UObject* WorldContextObject);

    UFUNCTION(BlueprintPure, Category = "UEP|Lyra", meta = (WorldContext = "WorldContextObject"))
    static FUEPLyraRuntimeSnapshot CaptureSnapshotForWorld(const UObject* WorldContextObject);
};
