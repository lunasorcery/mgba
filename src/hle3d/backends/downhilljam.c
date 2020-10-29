/*
High-Level Emulation of 3D engine from:
- Tony Hawk's Downhill Jam

Original renderer by Visual Impact
Emulation written by @lunasorcery
*/
#include <mgba/internal/arm/arm.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/hle3d/hle3d.h>
#include <mgba/hle3d/backends/downhilljam.h>

static uint32_t const
	kIdentDownhillJamEU = 0x50535842, // BXSP
	kIdentDownhillJamNA = 0x45535842; // BXSE

static bool const kDebugPrint = false;
static bool const kDebugDraw = false;

struct RenderParams {
	int scale;
	int rtWidth;
	int rtHeight;
	int rtTotalPixels;
};

static void ClearScreen(struct HLE3DBackendDownhillJam* backend, struct ARMCore* cpu, struct RenderParams const* params);
static void FlipBuffers(struct HLE3DBackendDownhillJam* backend, struct ARMCore* cpu);
static void FillColoredTrapezoid(struct HLE3DBackendDownhillJam* backend, struct ARMCore* cpu, struct RenderParams const* params);

void HLE3DBackendDownhillJamCreate(struct HLE3DBackendDownhillJam* backend)
{
	backend->b.init   = HLE3DBackendDownhillJamInit;
	backend->b.deinit = HLE3DBackendDownhillJamDeinit;
	backend->b.isGame = HLE3DBackendDownhillJamIsGame;
	backend->b.hook   = HLE3DBackendDownhillJamHook;
}

void HLE3DBackendDownhillJamInit(struct HLE3DBackend* backend, struct HLE3D* hle3d, uint32_t ident)
{
	UNUSED(ident);

	printf("[HLE3D/DownhillJam] Hooked Init\n");

	struct HLE3DBackendDownhillJam* thdjBackend = (struct HLE3DBackendDownhillJam*)backend;

	thdjBackend->addrFuncClearScreen       = 0x030007a0;
	thdjBackend->addrFuncFlipBuffers       = 0x08003536;
	thdjBackend->addrFuncFillColoredRegion = 0x0300227c;

	HLE3DAddBreakpoint(hle3d, thdjBackend->addrFuncClearScreen);
	HLE3DAddBreakpoint(hle3d, thdjBackend->addrFuncFlipBuffers);
	HLE3DAddBreakpoint(hle3d, thdjBackend->addrFuncFillColoredRegion);
}

void HLE3DBackendDownhillJamDeinit(struct HLE3DBackend* backend)
{
	UNUSED(backend);
}

bool HLE3DBackendDownhillJamIsGame(uint32_t ident)
{
	return
		(ident == kIdentDownhillJamEU) ||
		(ident == kIdentDownhillJamNA);
}

void HLE3DBackendDownhillJamHook(struct HLE3DBackend* backend, struct ARMCore* cpu, uint32_t pc)
{
	struct HLE3DBackendDownhillJam* thdjBackend = (struct HLE3DBackendDownhillJam*)backend;

	// prepare some parameters
	struct RenderParams params;
	params.scale = backend->h->renderScale;
	params.rtWidth = GBA_VIDEO_HORIZONTAL_PIXELS * params.scale;
	params.rtHeight = GBA_VIDEO_VERTICAL_PIXELS * params.scale;
	params.rtTotalPixels = params.rtWidth * params.rtHeight;

	if (pc == thdjBackend->addrFuncClearScreen) {
		ClearScreen(thdjBackend, cpu, &params);
		return;
	}

	if (pc == thdjBackend->addrFuncFlipBuffers) {
		FlipBuffers(thdjBackend, cpu);
		return;
	}

	if (pc == thdjBackend->addrFuncFillColoredRegion) {
		FillColoredTrapezoid(thdjBackend, cpu, &params);
		return;
	}

	printf("[HLE3D/DownhillJam] Unhandled hook at %08x\n", pc);
}

static void ClearScreen(struct HLE3DBackendDownhillJam* backend, struct ARMCore* cpu, struct RenderParams const* params)
{
	uint32_t const destPtr = cpu->gprs[0];
	uint8_t const destBufferIndex = (destPtr < 0x0600a000) ? 0 : 1;

	if (kDebugPrint) {
		printf("[HLE3D/DownhillJam] ---- clear screen %d ----\n", destBufferIndex);
	}

	backend->b.h->bgMode4active[destBufferIndex] = false;

	memset(backend->b.h->bgMode4pal[destBufferIndex], 0, params->rtTotalPixels);
}

static void FlipBuffers(struct HLE3DBackendDownhillJam* backend, struct ARMCore* cpu)
{
	uint16_t const value = cpu->gprs[0];
	uint8_t const mode = (value & 0x7);
	if (kDebugPrint) {
		printf("[HLE3D/DownhillJam] ---- flip buffers, mode %d frontbuffer %d (writing %04x to %08x) ----\n", mode, (value >> 4) & 1, value, cpu->gprs[0]);
	}
	if (mode == 4) {
		uint8_t const frontBufferIndex = (value >> 4) & 1;
		uint8_t const backBufferIndex = 1-frontBufferIndex;
		backend->b.h->bgMode4active[backBufferIndex] = false;
		HLE3DCommitMode4Buffer(backend->b.h, cpu, frontBufferIndex);
	} else {
		backend->b.h->bgMode4active[0] = false;
		backend->b.h->bgMode4active[1] = false;
	}
}

static void FillColoredTrapezoid(struct HLE3DBackendDownhillJam* backend, struct ARMCore* cpu, struct RenderParams const* params)
{
	uint32_t const destRowPtr = cpu->gprs[5];
	uint8_t const destBufferIndex = (destRowPtr < 0x0600a000) ? 0 : 1;
	int32_t const top = ((destRowPtr-0x06000000)%0xa000)/GBA_VIDEO_HORIZONTAL_PIXELS;

	uint8_t* renderTargetPal = backend->b.h->bgMode4pal[destBufferIndex];
	backend->b.h->bgMode4active[destBufferIndex] = true;

	int32_t const height = cpu->gprs[6];

	int32_t x0 = cpu->gprs[0] * params->scale;
	int32_t x1 = cpu->gprs[1] * params->scale;
	int32_t dx0 = cpu->gprs[7];
	int32_t dx1 = cpu->gprs[8];

	uint8_t const color = (cpu->gprs[4] >> 8);

	for (int y = 0; y < height*params->scale; ++y) {
		int left  = (x0 >> 15);
		int right = (x1 >> 15);
		if (left < 0) {
			left = 0;
		}
		if (right > GBA_VIDEO_HORIZONTAL_PIXELS*params->scale) {
			right = GBA_VIDEO_HORIZONTAL_PIXELS*params->scale;
		}
		if (left < right) {
			memset(&renderTargetPal[(top*params->scale+y)*params->rtWidth+left], color, right-left);
		}
		x0 += dx0;
		x1 += dx1;
	}
}
