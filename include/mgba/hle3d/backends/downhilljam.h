#pragma once
#include <mgba/internal/arm/arm.h>
#include <mgba/hle3d/backend.h>

struct HLE3DBackendDownhillJam {
	struct HLE3DBackend b;

	uint32_t addrFuncClearScreen;
	uint32_t addrFuncFlipBuffers;
	uint32_t addrFuncFillColoredRegion;
};

void HLE3DBackendDownhillJamCreate(struct HLE3DBackendDownhillJam* backend);

void HLE3DBackendDownhillJamInit(struct HLE3DBackend* backend, struct HLE3D* hle3d, uint32_t ident);
void HLE3DBackendDownhillJamDeinit(struct HLE3DBackend* backend);
bool HLE3DBackendDownhillJamIsGame(uint32_t ident);
void HLE3DBackendDownhillJamHook(struct HLE3DBackend* backend, struct ARMCore* cpu, uint32_t pc);
