#pragma once
#include <mgba/internal/arm/arm.h>
#include <mgba/hle3d/backend.h>

struct HLE3DBackendDrome {
	struct HLE3DBackend b;

	bool shouldHookTransform;
	bool shouldHookRasterize;

	bool disableRealTransform;
	bool disableRealRasterizer;

	// where does the renderer get loaded into ram
	// ie where do we ideally need to breakpoint?
	uint32_t addrRamExecutionPoint;

	// where does the game store the identity
	// of *which* function is in ram
	uint32_t addrRamActiveFunctionPtr;

	// where are the 3D functions in the rom?
	uint32_t addrRomTransformFunc;
	uint32_t addrRomRasterizeFunc;

	// where are the thumb-mode jumps to ram?
	//uint32_t addrRomJumpToTransform;
	//uint32_t addrRomJumpToRasterize;

	int spriteStackHeight;
	uint8_t spriteStack[32];
};

void HLE3DBackendDromeCreate(struct HLE3DBackendDrome* backend);

void HLE3DBackendDromeInit(struct HLE3DBackend* backend, struct HLE3D* hle3d, uint32_t ident);
void HLE3DBackendDromeDeinit(struct HLE3DBackend* backend);
bool HLE3DBackendDromeIsGame(uint32_t ident);
void HLE3DBackendDromeRefresh(struct HLE3DBackend* backend, struct GBA* gba, struct mCPUComponent* component);
void HLE3DBackendDromeHook(struct HLE3DBackend* backend, struct ARMCore* cpu);

void HLE3DBackendDromeHookTransform(struct HLE3DBackendDrome* backend, struct ARMCore* cpu);
void HLE3DBackendDromeHookRasterizer(struct HLE3DBackendDrome* backend, struct ARMCore* cpu);
void HLE3DBackendDromeRasterizeFlatTri(struct HLE3DBackendDrome* backend, struct ARMCore* cpu, uint32_t activeTriPtr, uint8_t* renderTarget);
void HLE3DBackendDromeRasterizeStaticTexTri(struct HLE3DBackendDrome* backend, struct ARMCore* cpu, uint32_t activeTriPtr, uint8_t* renderTarget);
void HLE3DBackendDromeRasterizeAffineTexTri(struct HLE3DBackendDrome* backend, struct ARMCore* cpu, uint32_t activeTriPtr, uint8_t* renderTarget);
void HLE3DBackendDromeRasterizeSpriteOccluder(struct HLE3DBackendDrome* backend, struct ARMCore* cpu, uint32_t activeTriPtr, uint8_t* renderTarget);
