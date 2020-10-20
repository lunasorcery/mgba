#pragma once
#include <mgba/internal/arm/arm.h>
#include <mgba/hle3d/backend.h>

struct HLE3DBackendAsterix {
	struct HLE3DBackend b;
};

void HLE3DBackendAsterixCreate(struct HLE3DBackendAsterix* backend);

void HLE3DBackendAsterixInit(struct HLE3DBackend* backend, struct HLE3D* hle3d, uint32_t ident);
void HLE3DBackendAsterixDeinit(struct HLE3DBackend* backend);
bool HLE3DBackendAsterixIsGame(uint32_t ident);
void HLE3DBackendAsterixHook(struct HLE3DBackend* backend, struct ARMCore* cpu, uint32_t pc);
