#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "UObject/Interface.h"

#include "PythonMixin.generated.h"

/** A named Python implementation that can be selected for one UObject instance. */
USTRUCT(BlueprintType)
struct UNREALENGINEPYTHON_API FUEPPythonMixinProfile
{
	GENERATED_BODY()

	/** Stable name used by the Blueprint instance and the runtime router. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Python Mixin")
	FName ProfileName = TEXT("Default");

	/** Importable module, for example "my_game.character_mixins". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Python Mixin")
	FString PythonModule;

	/** Class or dotted attribute path inside PythonModule. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Python Mixin")
	FString PythonClass;
};

/**
 * Declares every Python profile that may be used by a Blueprint class.
 *
 * Keeping the complete set in an asset makes the function union deterministic:
 * UEP can install one class-level dispatcher while choosing a Python callable per
 * UObject instance at runtime.
 */
UCLASS(BlueprintType)
class UNREALENGINEPYTHON_API UUEPPythonMixinSet : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Python Mixin")
	FName DefaultProfile = TEXT("Default");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Python Mixin")
	TArray<FUEPPythonMixinProfile> Profiles;
};

UINTERFACE(BlueprintType, Blueprintable)
class UNREALENGINEPYTHON_API UUEPPythonMixinInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Add this interface to a Blueprint that should use Python mixins.
 *
 * GetPythonMixinSet is normally constant for the Blueprint class (configure it
 * on the CDO). GetPythonMixinProfile may return an instance-editable Blueprint
 * variable, so two placed instances of the same BP class can select different
 * Python implementations without changing their Unreal type.
 */
class UNREALENGINEPYTHON_API IUEPPythonMixinInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Python Mixin")
	UUEPPythonMixinSet* GetPythonMixinSet() const;
	virtual UUEPPythonMixinSet* GetPythonMixinSet_Implementation() const { return nullptr; }

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Python Mixin")
	FName GetPythonMixinProfile() const;
	virtual FName GetPythonMixinProfile_Implementation() const { return NAME_None; }
};
