#include "UEPLyraWorldSubsystem.h"

#include "AbilitySystem/LyraAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/LyraHealthSet.h"
#include "Character/LyraHealthComponent.h"
#include "Character/LyraHeroComponent.h"
#include "Character/LyraPawnExtensionComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFeatureTypes.h"
#include "GameFeaturesSubsystem.h"
#include "GameplayEffect.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameModes/LyraExperienceDefinition.h"
#include "GameModes/LyraExperienceManagerComponent.h"
#include "LyraGameplayTags.h"
#include "System/LyraAssetManager.h"
#include "System/LyraGameData.h"

DEFINE_LOG_CATEGORY_STATIC(LogUEPLyraBridge, Log, All);

namespace
{
FString NetModeToString(const ENetMode NetMode)
{
    switch (NetMode)
    {
    case NM_Standalone:
        return TEXT("Standalone");
    case NM_DedicatedServer:
        return TEXT("DedicatedServer");
    case NM_ListenServer:
        return TEXT("ListenServer");
    case NM_Client:
        return TEXT("Client");
    default:
        return TEXT("Unknown");
    }
}

FString NetRoleToString(const ENetRole Role)
{
    switch (Role)
    {
    case ROLE_None:
        return TEXT("None");
    case ROLE_SimulatedProxy:
        return TEXT("SimulatedProxy");
    case ROLE_AutonomousProxy:
        return TEXT("AutonomousProxy");
    case ROLE_Authority:
        return TEXT("Authority");
    default:
        return TEXT("Unknown");
    }
}

bool IsValidGameplayCommandId(const FString& CommandId)
{
    if (CommandId.IsEmpty() || CommandId.Len() > 96)
    {
        return false;
    }

    for (const TCHAR Character : CommandId)
    {
        if (!FChar::IsAlnum(Character) && Character != TEXT('.') && Character != TEXT('_') && Character != TEXT('-'))
        {
            return false;
        }
    }
    return true;
}

APlayerController* FindGameplayCommandController(
    UWorld* World,
    const bool bTargetRemotePlayer,
    bool& bOutAmbiguous)
{
    bOutAmbiguous = false;
    if (!World)
    {
        return nullptr;
    }

    APlayerController* Match = nullptr;
    for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
    {
        APlayerController* Candidate = Iterator->Get();
        if (Candidate && Candidate->IsLocalController() != bTargetRemotePlayer)
        {
            if (Match)
            {
                bOutAmbiguous = true;
                return nullptr;
            }
            Match = Candidate;
        }
    }
    return Match;
}
}

bool UUEPLyraWorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

void UUEPLyraWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    InWorld.GameStateSetEvent.AddUObject(this, &ThisClass::HandleGameStateSet);
    AttachExperienceManager(InWorld.GetGameState());
}

void UUEPLyraWorldSubsystem::Deinitialize()
{
    check(IsInGameThread());
    if (UWorld* World = GetWorld())
    {
        World->GameStateSetEvent.RemoveAll(this);
    }
    OnExperienceReady.Clear();
    ExperienceManager.Reset();
    CurrentExperience.Reset();
    bExperienceCallbackRegistered = false;
    ConsumedGameplayCommandIds.Reset();
    Super::Deinitialize();
}

void UUEPLyraWorldSubsystem::AttachExperienceManager(AGameStateBase* GameState)
{
    check(IsInGameThread());
    UWorld* World = GetWorld();
    GameState = GameState ? GameState : (World ? World->GetGameState() : nullptr);
    ULyraExperienceManagerComponent* Manager = GameState ? GameState->FindComponentByClass<ULyraExperienceManagerComponent>() : nullptr;
    if (!Manager)
    {
        return;
    }
    if (bExperienceCallbackRegistered && ExperienceManager.Get() == Manager)
    {
        return;
    }

    CurrentExperience.Reset();
    ExperienceManager = Manager;
    bExperienceCallbackRegistered = true;
    Manager->CallOrRegister_OnExperienceLoaded(
        FOnLyraExperienceLoaded::FDelegate::CreateUObject(this, &ThisClass::HandleExperienceLoaded));
}

void UUEPLyraWorldSubsystem::HandleGameStateSet(AGameStateBase* GameState)
{
    AttachExperienceManager(GameState);
}

void UUEPLyraWorldSubsystem::HandleExperienceLoaded(const ULyraExperienceDefinition* Experience)
{
    check(IsInGameThread());
    CurrentExperience = const_cast<ULyraExperienceDefinition*>(Experience);
    OnExperienceReady.Broadcast(CurrentExperience.Get());
}

FUEPLyraRuntimeSnapshot UUEPLyraWorldSubsystem::CaptureSnapshot() const
{
    FUEPLyraRuntimeSnapshot Snapshot;
    Snapshot.bIsGameThread = IsInGameThread();
    if (!Snapshot.bIsGameThread)
    {
        UE_LOG(LogUEPLyraBridge, Error, TEXT("CaptureSnapshot must run on Unreal's game thread"));
        return Snapshot;
    }

    const UWorld* World = GetWorld();
    if (!World)
    {
        return Snapshot;
    }

    Snapshot.bIsGameWorld = World->IsGameWorld();
    Snapshot.WorldName = World->GetPathName();
    Snapshot.NetMode = NetModeToString(World->GetNetMode());
    Snapshot.bHasServerAuthority = World->GetNetMode() != NM_Client;

    APlayerController* PlayerController = nullptr;
    for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
    {
        APlayerController* Candidate = Iterator->Get();
        if (!Candidate)
        {
            continue;
        }
        ++Snapshot.PlayerControllerCount;
        if (Candidate->IsLocalController())
        {
            ++Snapshot.LocalPlayerControllerCount;
            PlayerController = Candidate;
        }
        else
        {
            ++Snapshot.RemotePlayerControllerCount;
            if (!PlayerController)
            {
                PlayerController = Candidate;
            }
        }
    }
    if (const AGameStateBase* GameState = World->GetGameState())
    {
        Snapshot.PlayerStateCount = GameState->PlayerArray.Num();
    }

    ULyraExperienceManagerComponent* Manager = ExperienceManager.Get();
    Snapshot.bHasExperienceManager = Manager != nullptr;
    Snapshot.bExperienceLoaded = Manager && Manager->IsExperienceLoaded();
    if (Snapshot.bExperienceLoaded)
    {
        Snapshot.CurrentExperience = const_cast<ULyraExperienceDefinition*>(Manager->GetCurrentExperienceChecked());
    }
    else
    {
        Snapshot.CurrentExperience = CurrentExperience.Get();
    }

    APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
    Snapshot.bHasPlayerPawn = Pawn != nullptr;
    if (Pawn)
    {
        Snapshot.bPawnLocallyControlled = Pawn->IsLocallyControlled();
        Snapshot.bPlayerStateReady = Pawn->GetPlayerState() != nullptr;
        Snapshot.PawnLocalRole = NetRoleToString(Pawn->GetLocalRole());
        Snapshot.PawnRemoteRole = NetRoleToString(Pawn->GetRemoteRole());
        const ULyraHeroComponent* Hero = ULyraHeroComponent::FindHeroComponent(Pawn);
        const ULyraPawnExtensionComponent* PawnExtension = ULyraPawnExtensionComponent::FindPawnExtensionComponent(Pawn);
        const ULyraHealthComponent* Health = ULyraHealthComponent::FindHealthComponent(Pawn);
        Snapshot.bHeroInputReady = Hero && Hero->IsReadyToBindInputs();
        Snapshot.bAbilitySystemReady = PawnExtension && PawnExtension->GetLyraAbilitySystemComponent();
        Snapshot.bHealthReady = Health && Health->GetMaxHealth() > 0.0f;
        Snapshot.bDamageImmune = PawnExtension && PawnExtension->GetLyraAbilitySystemComponent() &&
            PawnExtension->GetLyraAbilitySystemComponent()->HasMatchingGameplayTag(TAG_Gameplay_DamageImmunity);
        if (Snapshot.bHealthReady)
        {
            Snapshot.Health = Health->GetHealth();
            Snapshot.MaxHealth = Health->GetMaxHealth();
        }
    }

    UGameFeaturesSubsystem::Get().ForEachGameFeature(
        [&Snapshot](FGameFeatureInfo&& Info)
        {
            FUEPLyraGameFeatureState State;
            State.Name = MoveTemp(Info.Name);
            State.State = UE::GameFeatures::ToString(Info.CurrentState);
            State.bActive = Info.CurrentState == EGameFeaturePluginState::Active;
            Snapshot.GameFeatures.Add(MoveTemp(State));
        });
    Snapshot.GameFeatures.Sort(
        [](const FUEPLyraGameFeatureState& Left, const FUEPLyraGameFeatureState& Right)
        {
            return Left.Name < Right.Name;
        });
    return Snapshot;
}

FUEPLyraGameplayCommandResult UUEPLyraWorldSubsystem::ApplyAuthorityHealthDelta(
    const FString& CommandId,
    const float HealthDelta,
    const bool bTargetRemotePlayer)
{
    FUEPLyraGameplayCommandResult Result;
    Result.CommandId = CommandId;
    Result.RequestedHealthDelta = HealthDelta;

    const auto Reject = [&Result](const TCHAR* Status)
    {
        Result.Status = Status;
        return Result;
    };

    if (!IsInGameThread())
    {
        return Reject(TEXT("RejectedOffGameThread"));
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return Reject(TEXT("RejectedNoWorld"));
    }

    Result.bServerAuthority = World->GetNetMode() != NM_Client;
    if (!Result.bServerAuthority)
    {
        return Reject(TEXT("RejectedNotAuthority"));
    }
    if (!IsValidGameplayCommandId(CommandId))
    {
        return Reject(TEXT("RejectedInvalidCommandId"));
    }
    if (ConsumedGameplayCommandIds.Contains(CommandId))
    {
        return Reject(TEXT("RejectedDuplicateCommand"));
    }
    if (!FMath::IsFinite(HealthDelta) || FMath::IsNearlyZero(HealthDelta) || FMath::Abs(HealthDelta) > 25.0f)
    {
        return Reject(TEXT("RejectedInvalidMagnitude"));
    }

    bool bAmbiguousTarget = false;
    APlayerController* TargetController = FindGameplayCommandController(
        World,
        bTargetRemotePlayer,
        bAmbiguousTarget);
    if (bAmbiguousTarget)
    {
        return Reject(TEXT("RejectedAmbiguousTarget"));
    }
    APawn* TargetPawn = TargetController ? TargetController->GetPawn() : nullptr;
    if (!TargetPawn)
    {
        return Reject(TEXT("RejectedNoTarget"));
    }
    Result.TargetActor = TargetPawn->GetPathName();
    if (!TargetPawn->HasAuthority())
    {
        return Reject(TEXT("RejectedTargetNotAuthority"));
    }

    const ULyraPawnExtensionComponent* PawnExtension = ULyraPawnExtensionComponent::FindPawnExtensionComponent(TargetPawn);
    ULyraAbilitySystemComponent* AbilitySystem = PawnExtension ? PawnExtension->GetLyraAbilitySystemComponent() : nullptr;
    if (!AbilitySystem)
    {
        return Reject(TEXT("RejectedNoAbilitySystem"));
    }

    ULyraHealthComponent* Health = ULyraHealthComponent::FindHealthComponent(TargetPawn);
    if (!Health || Health->GetMaxHealth() <= 0.0f || Health->GetHealth() <= 0.0f)
    {
        return Reject(TEXT("RejectedNoHealth"));
    }

    Result.HealthBefore = Health->GetHealth();
    Result.HealthAfter = Result.HealthBefore;
    Result.MaxHealth = Health->GetMaxHealth();
    const float ExpectedHealth = Result.HealthBefore + HealthDelta;
    if (HealthDelta < 0.0f && AbilitySystem->HasMatchingGameplayTag(TAG_Gameplay_DamageImmunity))
    {
        return Reject(TEXT("RejectedDamageImmune"));
    }
    if (HealthDelta < 0.0f && ExpectedHealth < 1.0f)
    {
        return Reject(TEXT("RejectedUnsafeDamage"));
    }
    if (HealthDelta > 0.0f && ExpectedHealth > Result.MaxHealth)
    {
        return Reject(TEXT("RejectedUnsafeHeal"));
    }

    TSubclassOf<UGameplayEffect> GameplayEffectClass;
    if (HealthDelta < 0.0f)
    {
        GameplayEffectClass = ULyraAssetManager::GetSubclass(ULyraGameData::Get().DamageGameplayEffect_SetByCaller);
    }
    else
    {
        GameplayEffectClass = ULyraAssetManager::GetSubclass(ULyraGameData::Get().HealGameplayEffect_SetByCaller);
    }
    if (!GameplayEffectClass)
    {
        return Reject(TEXT("RejectedNoGameplayEffect"));
    }

    FGameplayEffectSpecHandle SpecHandle = AbilitySystem->MakeOutgoingSpec(
        GameplayEffectClass,
        1.0f,
        AbilitySystem->MakeEffectContext());
    if (!SpecHandle.IsValid())
    {
        return Reject(TEXT("RejectedSpecCreation"));
    }

    ConsumedGameplayCommandIds.Add(CommandId);
    Result.bAccepted = true;
    if (HealthDelta < 0.0f)
    {
        SpecHandle.Data->SetSetByCallerMagnitude(LyraGameplayTags::SetByCaller_Damage, -HealthDelta);
    }
    else
    {
        SpecHandle.Data->SetSetByCallerMagnitude(LyraGameplayTags::SetByCaller_Heal, HealthDelta);
    }
    AbilitySystem->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());

    Result.HealthAfter = Health->GetHealth();
    const float ObservedDelta = Result.HealthAfter - Result.HealthBefore;
    if (FMath::IsNearlyZero(ObservedDelta))
    {
        Result.Status = TEXT("FailedNoObservedChange");
        return Result;
    }
    if (!FMath::IsNearlyEqual(ObservedDelta, HealthDelta, 0.01f))
    {
        Result.Status = TEXT("FailedUnexpectedDelta");
        return Result;
    }

    Result.Status = TEXT("Applied");
    Result.bApplied = true;
    UE_LOG(
        LogUEPLyraBridge,
        Display,
        TEXT("UEP_LYRA_GAMEPLAY_COMMAND_APPLIED id=%s target=%s delta=%.2f health=%.2f/%.2f"),
        *Result.CommandId,
        *Result.TargetActor,
        Result.RequestedHealthDelta,
        Result.HealthAfter,
        Result.MaxHealth);
    return Result;
}

void UUEPLyraWorldSubsystem::ClearPythonListeners()
{
    if (!IsInGameThread())
    {
        UE_LOG(LogUEPLyraBridge, Error, TEXT("ClearPythonListeners must run on Unreal's game thread"));
        return;
    }
    OnExperienceReady.Clear();
}

UUEPLyraWorldSubsystem* UUEPLyraBridgeLibrary::GetBridgeForWorld(const UObject* WorldContextObject)
{
    if (!IsInGameThread() || !GEngine || !WorldContextObject)
    {
        return nullptr;
    }
    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    return World ? World->GetSubsystem<UUEPLyraWorldSubsystem>() : nullptr;
}

FUEPLyraRuntimeSnapshot UUEPLyraBridgeLibrary::CaptureSnapshotForWorld(const UObject* WorldContextObject)
{
    if (UUEPLyraWorldSubsystem* Bridge = GetBridgeForWorld(WorldContextObject))
    {
        return Bridge->CaptureSnapshot();
    }
    return FUEPLyraRuntimeSnapshot();
}
