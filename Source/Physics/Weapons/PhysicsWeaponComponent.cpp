// Copyright Epic Games, Inc. All Rights Reserved.


#include "PhysicsWeaponComponent.h"
#include "PhysicsCharacter.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Animation/AnimInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Components/PrimitiveComponent.h"
#include "PhysicsProjectile.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include <Components/SphereComponent.h>
#include <Camera/CameraComponent.h>
#include "GameFramework/DamageType.h"

// Sets default values for this component's properties
UPhysicsWeaponComponent::UPhysicsWeaponComponent()
{
	// Default offset from the character location for projectiles to spawn
	MuzzleOffset = FVector(100.0f, 0.0f, 10.0f);
	m_ImpulseStrength = 10000.0f;
}

void UPhysicsWeaponComponent::BeginPlay()
{
	Super::BeginPlay();
	ActorsToIgnore = { Character };
}


void UPhysicsWeaponComponent::Fire()
{
	if (Character == nullptr || Character->GetController() == nullptr)
	{
		return;
	}
	
	// Try and play the sound if specified
	if (FireSound != nullptr)
	{
		UGameplayStatics::PlaySoundAtLocation(this, FireSound, Character->GetActorLocation());
	}
	
	// Try and play a firing animation if specified
	if (FireAnimation != nullptr)
	{
		// Get the animation object for the arms mesh
		UAnimInstance* AnimInstance = Character->GetMesh1P()->GetAnimInstance();
		if (AnimInstance != nullptr)
		{
			AnimInstance->Montage_Play(FireAnimation, 1.f);
		}
	}
}
void UPhysicsWeaponComponent::ApplyDamage(AActor* OtherActor, FHitResult HitInfo, APhysicsProjectile* Projectile)
{
	if (!OtherActor || !m_WeaponDamageType)
	{
		return;
	}
	UPrimitiveComponent* HitComponent = HitInfo.GetComponent();

	AActor* DamageCauser = nullptr;
	if (Projectile)
	{
		
		DamageCauser = Projectile;
	}
	else
	{
		DamageCauser = Character;
	}

	TSubclassOf<UDamageType> DamageTypeClass = m_WeaponDamageType->m_DamageType;

	
	switch (m_WeaponDamageType->m_ImpulseType)
	{
	case EImpulseType::RAY:
		UGameplayStatics::ApplyPointDamage(OtherActor, m_WeaponDamageType->m_Damage, HitInfo.ImpactNormal * -1.0f, HitInfo, Character->GetController(), DamageCauser, DamageTypeClass);
		break;
	case EImpulseType::POINT:
		
		UGameplayStatics::ApplyPointDamage(OtherActor, m_WeaponDamageType->m_Damage, HitInfo.ImpactNormal * -1.0f, HitInfo, Character->GetController(), DamageCauser, DamageTypeClass);
		break;
	case EImpulseType::RADIAL:
		
		if (Projectile)
		{
			UGameplayStatics::ApplyRadialDamage(GetWorld(), m_WeaponDamageType->m_Damage, Projectile->GetActorLocation(), Projectile->m_Radius, DamageTypeClass, ActorsToIgnore, Projectile, Character->GetController());
		}
		break;
	default:
		
		UGameplayStatics::ApplyDamage(OtherActor, m_WeaponDamageType->m_Damage, Character->GetController(), Character, DamageTypeClass);
		break;
	}

	
	if (HitComponent && HitComponent->IsSimulatingPhysics())
	{
		switch (m_WeaponDamageType->m_ImpulseType)
		{
		case EImpulseType::RAY:
			
			HitComponent->AddImpulseAtLocation(HitInfo.ImpactNormal * -1.0f * m_ImpulseStrength, HitInfo.ImpactPoint, HitInfo.BoneName);
			break;
		case EImpulseType::POINT:
			
			if (Projectile)
			{
				
				HitComponent->AddImpulseAtLocation(Projectile->GetVelocity().GetSafeNormal() * m_ImpulseStrength, HitInfo.ImpactPoint, HitInfo.BoneName);
			}
			break;
		}
	}
	if (m_WeaponDamageType->m_ImpulseType == EImpulseType::RADIAL && Projectile)
	{
		TArray<AActor*> OverlappedActors;

		TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
		ObjectTypes.Add(EObjectTypeQuery::ObjectTypeQuery4);

		ActorsToIgnore.Add(Projectile);

		bool bHasOverlapped = UKismetSystemLibrary::SphereOverlapActors(
			GetWorld(),
			Projectile->GetActorLocation(),
			Projectile->m_Radius,
			ObjectTypes,
			AActor::StaticClass(),
			ActorsToIgnore,
			OverlappedActors
		);

		if (bHasOverlapped)
		{
			for (AActor* OverlappedActor : OverlappedActors)
			{
			
				TArray<UPrimitiveComponent*> PrimitiveComponents;
				OverlappedActor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

				for (UPrimitiveComponent* OverlappedComponent : PrimitiveComponents)
				{
					if (OverlappedComponent && OverlappedComponent->IsSimulatingPhysics())
					{
				
						OverlappedComponent->AddRadialForce(
							Projectile->GetActorLocation(),
							Projectile->m_Radius,
							m_ImpulseStrength,
							ERadialImpulseFalloff::RIF_Linear,
							true
						);
					}
				}
			}
		}
	}
}


bool UPhysicsWeaponComponent::AttachWeapon(APhysicsCharacter* TargetCharacter)
{
	Character = TargetCharacter;

	// Check that the character is valid, and has no weapon component yet
	if (Character == nullptr || Character->GetInstanceComponents().FindItemByClass<UPhysicsWeaponComponent>())
	{
		return false;
	}

	// Attach the weapon to the First Person Character
	FAttachmentTransformRules AttachmentRules(EAttachmentRule::SnapToTarget, true);
	AttachToComponent(Character->GetMesh1P(), AttachmentRules, FName(TEXT("GripPoint")));

	// Set up action bindings
	if (APlayerController* PlayerController = Cast<APlayerController>(Character->GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			// Set the priority of the mapping to 1, so that it overrides the Jump action with the Fire action when using touch input
			Subsystem->AddMappingContext(FireMappingContext, 1);
		}

		if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerController->InputComponent))
		{
			// Fire
			EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Triggered, this, &UPhysicsWeaponComponent::Fire);
		}
	}

	return true;
}

void UPhysicsWeaponComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// ensure we have a character owner
	if (Character != nullptr)
	{
		// remove the input mapping context from the Player Controller
		if (APlayerController* PlayerController = Cast<APlayerController>(Character->GetController()))
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
			{
				Subsystem->RemoveMappingContext(FireMappingContext);
			}
		}
	}

	// maintain the EndPlay call chain
	Super::EndPlay(EndPlayReason);
}