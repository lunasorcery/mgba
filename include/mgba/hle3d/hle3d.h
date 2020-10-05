#pragma once
#include <mgba/core/cpu.h>
#include <mgba/core/core.h>
#include <mgba/hle3d/backend.h>
#include <mgba/hle3d/backends/drome.h>
#include <mgba/internal/arm/arm.h>
#include <mgba-util/vfs.h>

struct HLE3DBreakpoint {
	struct HLE3DBreakpoint* next;
	uint32_t address;
};

struct HLE3D {
	struct HLE3DBreakpoint* breakpoints;

	struct HLE3DBackend* activeBackend;
	struct HLE3DBackendDrome backendDromeRacers;

	int renderScale;
	uint8_t* backgroundMode4pal[2];
	uint8_t* backgroundMode4color[2];
};

void HLE3DCreate(struct HLE3D* hle3d);
void HLE3DDestroy(struct HLE3D* hle3d);
void HLE3DSetRenderScale(struct HLE3D* hle3d, int scale);
void HLE3DOnLoadROM(struct HLE3D* hle3d, struct VFile* vf);
void HLE3DOnUnloadROM(struct HLE3D* hle3d);
void HLE3DHook(struct HLE3D* hle3d, struct ARMCore* cpu);
void HLE3DAddBreakpoint(struct HLE3D* hle3d, uint32_t address);
void HLE3DClearBreakpoints(struct HLE3D* hle3d);
void HLE3DCheckBreakpoints(struct HLE3D* hle3d, struct ARMCore* cpu);
