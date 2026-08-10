#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UEPValidationTypes.generated.h"

UENUM(BlueprintType)
enum class EUEPValidationChoice : uint8
{
    First,
    Second,
    Third
};

USTRUCT(BlueprintType)
struct UEP58HOST_API FUEPValidationStruct
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEP Validation")
    int32 Count = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEP Validation")
    FString Label;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEP Validation")
    FVector Vector = FVector::ZeroVector;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUEPValidationSignal, int32, Value);

UCLASS(BlueprintType)
class UEP58HOST_API UUEPValidationObject : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEP Validation")
    bool BoolValue = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEP Validation")
    int32 Int32Value = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEP Validation")
    int64 Int64Value = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEP Validation")
    float FloatValue = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEP Validation")
    double DoubleValue = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEP Validation")
    FString StringValue;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEP Validation")
    FName NameValue;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEP Validation")
    FText TextValue;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEP Validation")
    FVector VectorValue = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEP Validation")
    FRotator RotatorValue = FRotator::ZeroRotator;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEP Validation")
    FTransform TransformValue = FTransform::Identity;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEP Validation")
    TArray<int32> IntArray;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEP Validation")
    TSet<FString> StringSet;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEP Validation")
    TMap<FString, int32> StringIntMap;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEP Validation")
    FUEPValidationStruct StructValue;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEP Validation")
    EUEPValidationChoice ChoiceValue = EUEPValidationChoice::First;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEP Validation")
    TObjectPtr<UObject> ObjectValue;

    UPROPERTY(BlueprintAssignable, Category = "UEP Validation")
    FUEPValidationSignal OnSignal;

    UFUNCTION(BlueprintCallable, Category = "UEP Validation")
    int32 AddIntegers(int32 A, int32 B) const;

    UFUNCTION(BlueprintCallable, Category = "UEP Validation")
    bool ComputeOutputs(int32 Input, int32& Doubled, FString& Label) const;

    UFUNCTION(BlueprintCallable, Category = "UEP Validation")
    FUEPValidationStruct EchoStruct(const FUEPValidationStruct& Input) const;

    UFUNCTION(BlueprintCallable, Category = "UEP Validation")
    void BroadcastSignal(int32 Value);
};

UCLASS()
class UEP58HOST_API AUEPValidationActor : public AActor
{
    GENERATED_BODY()

public:
    AUEPValidationActor();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEP Validation")
    int32 Counter = 0;

    UFUNCTION(BlueprintCallable, Category = "UEP Validation")
    int32 IncrementCounter(int32 Amount);
};
