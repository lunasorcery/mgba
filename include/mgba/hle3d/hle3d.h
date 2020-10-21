#pragma once
#include <mgba/core/cpu.h>
#include <mgba/core/core.h>
#include <mgba/internal/arm/arm.h>
#include <mgba-util/vfs.h>

#include <mgba/hle3d/backend.h>
#include <mgba/hle3d/backends/v3d.h>
#include <mgba/hle3d/backends/drome.h>

struct HLE3DBreakpoint {
	struct HLE3DBreakpoint* next;
	uint32_t address;
};

struct HLE3DDebugRect {
	struct HLE3DDebugRect* next;
	int16_t x,y;
	uint16_t w,h;
	uint32_t rgb;
};

struct HLE3D {
	struct HLE3DBreakpoint* breakpoints;

	struct HLE3DBackend* activeBackend;
	struct HLE3DBackendV3D backendV3D;
	struct HLE3DBackendDrome backendDromeRacers;

	int renderScale;

	bool bgMode4active[2];
	uint8_t* bgMode4pal[2];
	uint8_t* bgMode4color[2];

	struct HLE3DDebugRect* debugRects;
};

void HLE3DCreate(struct HLE3D* hle3d);
void HLE3DDestroy(struct HLE3D* hle3d);
void HLE3DSetRenderScale(struct HLE3D* hle3d, int scale);
void HLE3DOnLoadROM(struct HLE3D* hle3d, struct VFile* vf);
void HLE3DOnUnloadROM(struct HLE3D* hle3d);
void HLE3DHook(struct HLE3D* hle3d, struct ARMCore* cpu, uint32_t pc);
void HLE3DAddBreakpoint(struct HLE3D* hle3d, uint32_t address);
void HLE3DClearBreakpoints(struct HLE3D* hle3d);
void HLE3DCheckBreakpoints(struct HLE3D* hle3d, struct ARMCore* cpu);
void HLE3DCommitMode4Buffer(struct HLE3D* hle3d, struct ARMCore* cpu, uint8_t frame);
void HLE3DDebugDrawRect(struct HLE3D* hle3d, int16_t x, int16_t y, uint16_t w, uint16_t h, uint32_t color);
void HLE3DDebugClear(struct HLE3D* hle3d);
