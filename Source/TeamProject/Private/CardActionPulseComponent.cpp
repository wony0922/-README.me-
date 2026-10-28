#include "CardActionPulseComponent.h"

#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

const FName UCardActionPulseComponent::PulseOpacityParameter(TEXT("PulseOpacity"));

UCardActionPulseComponent::UCardActionPulseComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UCardActionPulseComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		SetComponentTickEnabled(false);
		return;
	}

	TArray<UStaticMeshComponent*> Meshes;
	Owner->GetComponents<UStaticMeshComponent>(Meshes);
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		if (!Mesh || !Mesh->GetName().Contains(TEXT("Glow")))
		{
			continue;
		}

		FPulseTarget& Target = Targets.AddDefaulted_GetRef();
		Target.Component = Mesh;
		Target.BaseScale = Mesh->GetRelativeScale3D();
		for (int32 MaterialIndex = 0; MaterialIndex < Mesh->GetNumMaterials(); ++MaterialIndex)
		{
			Target.Materials.Add(Mesh->CreateAndSetMaterialInstanceDynamic(MaterialIndex));
		}
	}

	if (Targets.IsEmpty())
	{
		SetComponentTickEnabled(false);
	}
}

void UCardActionPulseComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	for (FPulseTarget& Target : Targets)
	{
		UStaticMeshComponent* Mesh = Target.Component.Get();
		if (!Mesh)
		{
			continue;
		}

		const bool bVisible = Mesh->IsVisible() && !Mesh->bHiddenInGame;
		if (!bVisible)
		{
			if (Target.bAnimating)
			{
				ResetTarget(Target, false);
			}
			Target.bWasVisible = false;
			continue;
		}

		if (bVisible && !Target.bWasVisible)
		{
			Target.Elapsed = 0.0f;
			Target.bAnimating = true;
		}
		Target.bWasVisible = bVisible;

		if (!Target.bAnimating)
		{
			continue;
		}

		Target.Elapsed += DeltaTime;
		const float Alpha = FMath::Clamp(Target.Elapsed / PulseDuration, 0.0f, 1.0f);
		const float Scale = FMath::Lerp(1.0f, PulseScale, Alpha);
		Mesh->SetRelativeScale3D(Target.BaseScale * Scale);
		for (UMaterialInstanceDynamic* Material : Target.Materials)
		{
			if (Material)
			{
				Material->SetScalarParameterValue(PulseOpacityParameter, 1.0f - Alpha);
			}
		}

		if (Alpha >= 1.0f)
		{
			Target.Elapsed = 0.0f;
		}
	}
}

void UCardActionPulseComponent::ResetTarget(FPulseTarget& Target, bool bHide)
{
	if (UStaticMeshComponent* Mesh = Target.Component.Get())
	{
		Mesh->SetRelativeScale3D(Target.BaseScale);
		if (bHide)
		{
			Mesh->SetVisibility(false, true);
		}
	}

	for (UMaterialInstanceDynamic* Material : Target.Materials)
	{
		if (Material)
		{
			Material->SetScalarParameterValue(PulseOpacityParameter, 1.0f);
		}
	}
	Target.Elapsed = 0.0f;
	Target.bAnimating = false;
	Target.bWasVisible = false;
}
