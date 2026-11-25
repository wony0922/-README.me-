#include "OreDealMerchantStand.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

AOreDealMerchantStand::AOreDealMerchantStand()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);

	Tags.AddUnique(TEXT("OreDealMerchant"));

	StandRoot = CreateDefaultSubobject<USceneComponent>(TEXT("StandRoot"));
	RootComponent = StandRoot;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMeshFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BodyMeshFinder(TEXT("/Game/Asset/Char2/SM_Char2_Body_Cowboy.SM_Char2_Body_Cowboy"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> HeadMeshFinder(TEXT("/Game/Asset/Char2/SM_Char2_Head_Cowboy.SM_Char2_Head_Cowboy"));

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WoodMatFinder(TEXT("/Game/Asset/Saloon/SL_WoodDark.SL_WoodDark"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> RoofMatFinder(TEXT("/Game/Asset/Saloon/SL_Roof.SL_Roof"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BrassMatFinder(TEXT("/Game/Asset/Saloon/SL_Brass.SL_Brass"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> CounterMatFinder(TEXT("/Game/Asset/Saloon/SL_BarWood.SL_BarWood"));

	UStaticMesh* CubeMesh = CubeMeshFinder.Succeeded() ? CubeMeshFinder.Object : nullptr;
	UStaticMesh* CylinderMesh = CylinderMeshFinder.Succeeded() ? CylinderMeshFinder.Object : nullptr;
	UStaticMesh* BodyMesh = BodyMeshFinder.Succeeded() ? BodyMeshFinder.Object : nullptr;
	UStaticMesh* HeadMesh = HeadMeshFinder.Succeeded() ? HeadMeshFinder.Object : nullptr;

	UMaterialInterface* WoodMat = WoodMatFinder.Succeeded() ? WoodMatFinder.Object : nullptr;
	UMaterialInterface* RoofMat = RoofMatFinder.Succeeded() ? RoofMatFinder.Object : nullptr;
	UMaterialInterface* BrassMat = BrassMatFinder.Succeeded() ? BrassMatFinder.Object : nullptr;
	UMaterialInterface* CounterMat = CounterMatFinder.Succeeded() ? CounterMatFinder.Object : nullptr;

	// 1. Lower Box (TableBase)
	TableBase = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TableBase"));
	TableBase->SetupAttachment(StandRoot);
	if (CubeMesh) TableBase->SetStaticMesh(CubeMesh);
	if (WoodMat) TableBase->SetMaterial(0, WoodMat);
	TableBase->SetRelativeLocation(FVector(0.0f, 0.0f, 10.0f));
	TableBase->SetRelativeScale3D(FVector(0.18f, 0.12f, 0.16f)); // 18cm long x 12cm wide x 16cm high
	TableBase->SetCollisionProfileName(TEXT("BlockAll"));

	// 2. Counter Top Surface
	CounterTop = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CounterTop"));
	CounterTop->SetupAttachment(StandRoot);
	if (CubeMesh) CounterTop->SetStaticMesh(CubeMesh);
	if (CounterMat) CounterTop->SetMaterial(0, CounterMat);
	CounterTop->SetRelativeLocation(FVector(0.0f, 0.0f, 18.5f));
	CounterTop->SetRelativeScale3D(FVector(0.22f, 0.15f, 0.015f));
	CounterTop->SetCollisionProfileName(TEXT("BlockAll"));

	// 3. 4 Corner Posts (Brass)
	const float PostX = 8.0f;
	const float PostY = 5.5f;
	const float PostHeight = 15.0f;
	const FVector PostScale(0.012f, 0.012f, PostHeight * 0.01f);

	auto SetupPost = [&](TObjectPtr<UStaticMeshComponent>& Post, const TCHAR* Name, float X, float Y)
	{
		Post = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Post->SetupAttachment(StandRoot);
		if (CylinderMesh) Post->SetStaticMesh(CylinderMesh);
		if (BrassMat) Post->SetMaterial(0, BrassMat);
		Post->SetRelativeLocation(FVector(X, Y, 18.5f + PostHeight * 0.5f));
		Post->SetRelativeScale3D(PostScale);
		Post->SetCollisionProfileName(TEXT("BlockAll"));
	};

	SetupPost(PostFL, TEXT("PostFL"), PostX, -PostY);
	SetupPost(PostFR, TEXT("PostFR"), PostX, PostY);
	SetupPost(PostBL, TEXT("PostBL"), -PostX, -PostY);
	SetupPost(PostBR, TEXT("PostBR"), -PostX, PostY);

	// 4. Roof Awning
	RoofAwning = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RoofAwning"));
	RoofAwning->SetupAttachment(StandRoot);
	if (CubeMesh) RoofAwning->SetStaticMesh(CubeMesh);
	if (RoofMat) RoofAwning->SetMaterial(0, RoofMat);
	RoofAwning->SetRelativeLocation(FVector(0.0f, 0.0f, 18.5f + PostHeight + 2.0f));
	RoofAwning->SetRelativeScale3D(FVector(0.24f, 0.17f, 0.035f));
	RoofAwning->SetCollisionProfileName(TEXT("BlockAll"));

	// 5. Wheels on sides (classic cart wheels)
	WheelL = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WheelL"));
	WheelL->SetupAttachment(StandRoot);
	if (CylinderMesh) WheelL->SetStaticMesh(CylinderMesh);
	if (WoodMat) WheelL->SetMaterial(0, WoodMat);
	WheelL->SetRelativeLocation(FVector(0.0f, -6.8f, 7.0f));
	WheelL->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
	WheelL->SetRelativeScale3D(FVector(0.12f, 0.12f, 0.015f));
	WheelL->SetCollisionProfileName(TEXT("BlockAll"));

	WheelR = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WheelR"));
	WheelR->SetupAttachment(StandRoot);
	if (CylinderMesh) WheelR->SetStaticMesh(CylinderMesh);
	if (WoodMat) WheelR->SetMaterial(0, WoodMat);
	WheelR->SetRelativeLocation(FVector(0.0f, 6.8f, 7.0f));
	WheelR->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
	WheelR->SetRelativeScale3D(FVector(0.12f, 0.12f, 0.015f));
	WheelR->SetCollisionProfileName(TEXT("BlockAll"));

	// 6. Mini Merchant Character standing inside/behind counter
	MerchantBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MerchantBody"));
	MerchantBody->SetupAttachment(StandRoot);
	if (BodyMesh) MerchantBody->SetStaticMesh(BodyMesh);
	MerchantBody->SetRelativeLocation(FVector(-3.0f, 0.0f, 14.0f));
	MerchantBody->SetRelativeRotation(FRotator(0.0f, 0.0f, 0.0f)); // Facing forward (+X)
	MerchantBody->SetRelativeScale3D(FVector(0.25f, 0.25f, 0.25f));
	MerchantBody->SetCollisionProfileName(TEXT("BlockAll"));

	MerchantHead = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MerchantHead"));
	MerchantHead->SetupAttachment(MerchantBody);
	if (HeadMesh) MerchantHead->SetStaticMesh(HeadMesh);
	MerchantHead->SetRelativeLocation(FVector(0.0f, 0.0f, 39.0f)); // Neck pivot
	MerchantHead->SetCollisionProfileName(TEXT("BlockAll"));
}

void AOreDealMerchantStand::BeginPlay()
{
	Super::BeginPlay();
}
