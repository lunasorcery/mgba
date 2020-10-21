/*
High-Level Emulation of 3D engine from:
- Asterix & Obelix XXL (2004)
- Driv3r (2005)

Original 'V3D' renderer by Fernando Velez & Guillaume Dubail
Emulation written by @lunasorcery
*/
#include <mgba/internal/arm/arm.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/hle3d/hle3d.h>
#include <mgba/hle3d/backends/v3d.h>

static uint32_t const
	kIdentAsterixXXL     = 0x50584c42, // BLXP
	kIdentAsterixXXL2in1 = 0x50413242, // B2AP
	kIdentDriv3rEU       = 0x50523342, // B3RP
	kIdentDriv3rNA       = 0x45523342; // B3RE

static bool const kDebugDraw = false;

struct RenderParams {
	uint8_t frontBufferIndex;
	uint8_t backBufferIndex;
	int scale;
	int rtWidth;
	int rtHeight;
	int rtTotalPixels;
};

static void ClearScreen(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params);
static void CopyScreen(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params);
static void FlipBuffers(struct HLE3DBackendV3D* backend, struct ARMCore* cpu);
static void FillColoredTrapezoid(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params);
static void FillTexturedTrapezoid(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params, uint32_t addrUv0, uint32_t addrUv1, uint32_t addrUvRowDelta0, uint32_t addrUvRowDelta1);

static void DrawAsterixPlayerSprite(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params, uint8_t paletteMask);
static void DrawAsterixScaledEnvSprite(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params);
static void DrawAsterixScaledNpcSprite(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params);

static void DrawDriv3rPlayerSprite(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params);
static void DrawDriv3rScaledSprite(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params);


void HLE3DBackendV3DCreate(struct HLE3DBackendV3D* backend)
{
	backend->b.init   = HLE3DBackendV3DInit;
	backend->b.deinit = HLE3DBackendV3DDeinit;
	backend->b.isGame = HLE3DBackendV3DIsGame;
	backend->b.hook   = HLE3DBackendV3DHook;
}

void HLE3DBackendV3DInit(struct HLE3DBackend* backend, struct HLE3D* hle3d, uint32_t ident)
{
	struct HLE3DBackendV3D* v3dBackend = (struct HLE3DBackendV3D*)backend;

	// zero out the game-specific values
	v3dBackend->isAsterix = false;
	v3dBackend->addrFuncAsterixPlayerSprite0   = 0;
	v3dBackend->addrFuncAsterixPlayerSprite1   = 0;
	v3dBackend->addrFuncAsterixScaledEnvSprite = 0;
	v3dBackend->addrFuncAsterixScaledNpcSprite = 0;
	v3dBackend->addrFuncAsterixMenuOverlay     = 0;
	v3dBackend->addrFuncAsterixScreenCopyHorizontalScroll = 0;
	v3dBackend->addrFuncAsterixScreenCopyVerticalScroll   = 0;

	v3dBackend->isDriv3r = false;
	v3dBackend->addrFuncDriv3rPlayerSprite = 0;
	v3dBackend->addrFuncDriv3rScaledSprite = 0;

	if (ident == kIdentAsterixXXL || ident == kIdentAsterixXXL2in1)
	{
		v3dBackend->isAsterix = true;

		// shared

		v3dBackend->addrFuncClearScreen  = 0x03004198;
		v3dBackend->addrFuncCopyScreen   = 0x03006834;
		v3dBackend->addrScreenCopySource = 0x03006a00;
		v3dBackend->addrFuncFlipBuffers  = 0x030075b8;
		v3dBackend->addrActiveFrame      = 0x0203dc1b;

		v3dBackend->addrFuncTexture1pxTrapezoid = 0x03004940;
		v3dBackend->addrTex1UvRowDelta0 = 0x0300472c;
		v3dBackend->addrTex1UvRowDelta1 = 0x03004730;
		v3dBackend->addrTex1Uv0         = 0x03004734;
		v3dBackend->addrTex1Uv1         = 0x03004738;

		v3dBackend->addrFuncTexture2pxTrapezoid = 0x03004940;
		v3dBackend->addrTex2UvRowDelta0 = 0x0300472c;
		v3dBackend->addrTex2UvRowDelta1 = 0x03004730;
		v3dBackend->addrTex2Uv0         = 0x03004734;
		v3dBackend->addrTex2Uv1         = 0x03004738;

		v3dBackend->addrFuncColoredTrapezoid = 0x030044e8;
		v3dBackend->addrColoredPolyColor = 0x03004708;

		// game-specific

		v3dBackend->addrFuncAsterixPlayerSprite0   = 0x03005e0c;
		v3dBackend->addrFuncAsterixPlayerSprite1   = 0x03005f98;
		v3dBackend->addrFuncAsterixScaledEnvSprite = 0x03006144;
		v3dBackend->addrFuncAsterixScaledNpcSprite = 0x03006328;

		if (ident == kIdentAsterixXXL)
		{
			v3dBackend->addrFuncAsterixMenuOverlay = 0x0805c5f0;
		}
		if (ident == kIdentAsterixXXL2in1)
		{
			v3dBackend->addrFuncAsterixMenuOverlay = 0x0885f8f0;
		}

		v3dBackend->addrFuncAsterixScreenCopyHorizontalScroll = 0x030068c4;
		v3dBackend->addrFuncAsterixScreenCopyVerticalScroll   = 0x03006934;

		HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncAsterixPlayerSprite0);
		HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncAsterixPlayerSprite1);
		HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncAsterixScaledEnvSprite);
		HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncAsterixScaledNpcSprite);
		HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncAsterixMenuOverlay);
		HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncAsterixScreenCopyHorizontalScroll);
		HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncAsterixScreenCopyVerticalScroll);
	}
	else if (ident == kIdentDriv3rEU || ident == kIdentDriv3rNA)
	{
		v3dBackend->isDriv3r = true;

		// shared

		v3dBackend->addrFuncClearScreen  = 0x03004984;
		v3dBackend->addrFuncCopyScreen   = 0x03004a98;
		v3dBackend->addrScreenCopySource = 0x03004b2c;
		v3dBackend->addrFuncFlipBuffers  = 0x030078c0;
		v3dBackend->addrActiveFrame      = 0x0203ab41;

		v3dBackend->addrFuncTexture1pxTrapezoid = 0x03005454;
		v3dBackend->addrTex1UvRowDelta0 = 0x03005b34;
		v3dBackend->addrTex1UvRowDelta1 = 0x03005b38;
		v3dBackend->addrTex1Uv0         = 0x03005b3c;
		v3dBackend->addrTex1Uv1         = 0x03005b40;

		v3dBackend->addrFuncTexture2pxTrapezoid = 0x03005ccc;
		v3dBackend->addrTex2UvRowDelta0 = 0x030061d4;
		v3dBackend->addrTex2UvRowDelta1 = 0x030061d8;
		v3dBackend->addrTex2Uv0         = 0x030061dc;
		v3dBackend->addrTex2Uv1         = 0x030061e0;

		v3dBackend->addrFuncColoredTrapezoid = 0x03004ca8;
		v3dBackend->addrColoredPolyColor = 0x03004ed8;

		// game-specific

		v3dBackend->addrFuncDriv3rPlayerSprite = 0x030063d4;
		v3dBackend->addrFuncDriv3rScaledSprite = 0x030061e4;

		HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncDriv3rPlayerSprite);
		HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncDriv3rScaledSprite);
	}

	HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncClearScreen);
	HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncCopyScreen);
	HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncFlipBuffers);
	HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncColoredTrapezoid);
	HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncTexture1pxTrapezoid);
	HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncTexture2pxTrapezoid);
}

void HLE3DBackendV3DDeinit(struct HLE3DBackend* backend)
{
	UNUSED(backend);
}

bool HLE3DBackendV3DIsGame(uint32_t ident)
{
	return
		(ident == kIdentDriv3rEU) ||
		(ident == kIdentDriv3rNA) ||
		(ident == kIdentAsterixXXL) ||
		(ident == kIdentAsterixXXL2in1);
}

void HLE3DBackendV3DHook(struct HLE3DBackend* backend, struct ARMCore* cpu, uint32_t pc)
{
	struct HLE3DBackendV3D* v3dBackend = (struct HLE3DBackendV3D*)backend;

	// prepare some parameters
	uint8_t const frame = cpu->memory.load8(cpu, v3dBackend->addrActiveFrame, NULL);
	struct RenderParams params;
	params.frontBufferIndex = frame?1:0;
	params.backBufferIndex = 1-frame;
	params.scale = backend->h->renderScale;
	params.rtWidth = GBA_VIDEO_HORIZONTAL_PIXELS * params.scale;
	params.rtHeight = GBA_VIDEO_VERTICAL_PIXELS * params.scale;
	params.rtTotalPixels = params.rtWidth * params.rtHeight;

	// clear screen ahead of rendering
	if (pc == v3dBackend->addrFuncClearScreen) {
		ClearScreen(v3dBackend, cpu, &params);
		return;
	}

	// copy screen ahead of rendering (for bitmap backgrounds)
	if (pc == v3dBackend->addrFuncCopyScreen) {
		CopyScreen(v3dBackend, cpu, &params);
		return;
	}

	// flip buffers after rendering
	if (pc == v3dBackend->addrFuncFlipBuffers) {
		FlipBuffers(v3dBackend, cpu);
		return;
	}

	// color fill
	if (pc == v3dBackend->addrFuncColoredTrapezoid) {
		FillColoredTrapezoid(v3dBackend, cpu, &params);
		return;
	}

	// texture fill
	if (pc == v3dBackend->addrFuncTexture1pxTrapezoid) {
		FillTexturedTrapezoid(
			v3dBackend, cpu, &params,
			v3dBackend->addrTex1Uv0,
			v3dBackend->addrTex1Uv1,
			v3dBackend->addrTex1UvRowDelta0,
			v3dBackend->addrTex1UvRowDelta1);
		return;
	}

	// texture fill
	if (pc == v3dBackend->addrFuncTexture2pxTrapezoid) {
		FillTexturedTrapezoid(
			v3dBackend, cpu, &params,
			v3dBackend->addrTex2Uv0,
			v3dBackend->addrTex2Uv1,
			v3dBackend->addrTex2UvRowDelta0,
			v3dBackend->addrTex2UvRowDelta1);
		return;
	}

	if (v3dBackend->isAsterix)
	{
		// menu overlay overwrites the frontbuffer
		if (pc == v3dBackend->addrFuncAsterixMenuOverlay) {
			backend->h->bgMode4active[params.frontBufferIndex] = false;
			return;
		}

		// screen copies that overwrite 3D data
		if (pc == v3dBackend->addrFuncAsterixScreenCopyHorizontalScroll ||
		    pc == v3dBackend->addrFuncAsterixScreenCopyVerticalScroll) {
			backend->h->bgMode4active[params.backBufferIndex] = false;
			return;
		}

		if (pc == v3dBackend->addrFuncAsterixPlayerSprite0) {
			DrawAsterixPlayerSprite(v3dBackend, cpu, &params, 0x00);
			return;
		}

		if (pc == v3dBackend->addrFuncAsterixPlayerSprite1) {
			DrawAsterixPlayerSprite(v3dBackend, cpu, &params, 0x10);
			return;
		}

		if (pc == v3dBackend->addrFuncAsterixScaledEnvSprite) {
			DrawAsterixScaledEnvSprite(v3dBackend, cpu, &params);
			return;
		}

		if (pc == v3dBackend->addrFuncAsterixScaledNpcSprite) {
			DrawAsterixScaledNpcSprite(v3dBackend, cpu, &params);
			return;
		}
	}

	if (v3dBackend->isDriv3r)
	{
		if (pc == v3dBackend->addrFuncDriv3rPlayerSprite) {
			DrawDriv3rPlayerSprite(v3dBackend, cpu, &params);
			return;
		}

		if (pc == v3dBackend->addrFuncDriv3rScaledSprite) {
			DrawDriv3rScaledSprite(v3dBackend, cpu, &params);
			return;
		}
	}

	printf("[HLE3D/V3D] Unhandled hook at %08x\n", pc);
}

static void ClearScreen(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params)
{
	UNUSED(cpu);

	//printf("[HLE3D/V3D] ---- clear screen ----\n");

	backend->b.h->bgMode4active[params->backBufferIndex] = false;

	memset(backend->b.h->bgMode4pal[params->backBufferIndex], 0, params->rtTotalPixels);
}

static void CopyScreen(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params)
{
	//printf("[HLE3D/V3D] ---- copy screen ----\n");
	backend->b.h->bgMode4active[params->backBufferIndex] = false;

	uint32_t const srcAddr = cpu->memory.load32(cpu, backend->addrScreenCopySource, NULL);
	struct GBA* gba = (struct GBA*)cpu->master;
	struct GBAMemory* memory = &gba->memory;
	uint8_t const srcRegion = (srcAddr >> 24);
	uint8_t const* rawSrcPtr;
	if (srcRegion == 0x02)
	{
		rawSrcPtr = (uint8_t*)memory->wram + (srcAddr & 0xffffff);
	}
	else if (srcRegion == 0x03)
	{
		rawSrcPtr = (uint8_t*)memory->iwram + (srcAddr & 0xffffff);
	}
	else
	{
		printf("[HLE3D/V3D] failed screen copy from unsupported memory region %02x\n", (srcAddr >> 24));
		return;
	}

	if (params->scale == 1)
	{
		memcpy(backend->b.h->bgMode4pal[params->backBufferIndex], rawSrcPtr, params->rtTotalPixels);
	}
	else
	{
		uint8_t* rawDstPtr = backend->b.h->bgMode4pal[params->backBufferIndex];
		for (int y = 0; y < GBA_VIDEO_VERTICAL_PIXELS; ++y) {
			for (int sy = 0; sy < params->scale; ++sy) {
				for (int x = 0; x < GBA_VIDEO_HORIZONTAL_PIXELS; ++x) {
					for (int sx = 0; sx < params->scale; ++sx) {
						*rawDstPtr++ = rawSrcPtr[y*GBA_VIDEO_HORIZONTAL_PIXELS+x];
					}
				}
			}
		}
	}
}

static void FlipBuffers(struct HLE3DBackendV3D* backend, struct ARMCore* cpu)
{
	//printf("[HLE3D/V3D] ---- flip buffers ----\n");
	uint16_t const value = cpu->gprs[2];
	uint8_t const mode = (value & 0x7);
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

static void FillColoredTrapezoid(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params)
{
	backend->b.h->bgMode4active[params->backBufferIndex] = true;
	uint8_t* renderTargetPal = backend->b.h->bgMode4pal[params->backBufferIndex];

	uint8_t const color = cpu->memory.load8(cpu, backend->addrColoredPolyColor, NULL);

	uint32_t const r5  = cpu->gprs[5];  // height
	uint32_t const r7  = cpu->gprs[7];  // x1
	uint32_t const r8  = cpu->gprs[8];  // x0
	uint32_t const r10 = cpu->gprs[10]; // destination vram pointer

	uint16_t const x0 = (r8 >> 16);
	uint16_t const x1 = (r7 >> 16);
	int16_t const dx0 = (r8 & 0xffff);
	int16_t const dx1 = (r7 & 0xffff);

	int const top = ((r10-0x06000000)%0xa000)/GBA_VIDEO_HORIZONTAL_PIXELS;

	for (int y = 0; y < (int)r5*params->scale; ++y) {
		int const left = (x0*params->scale+(dx0*y)) >> 8;
		int const right = (x1*params->scale+(dx1*y)) >> 8;

		if (right > left) {
			memset(&renderTargetPal[(top*params->scale+y)*params->rtWidth+left], color, right-left);
		}
	}
}

static void FillTexturedTrapezoid(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params, uint32_t addrUv0, uint32_t addrUv1, uint32_t addrUvRowDelta0, uint32_t addrUvRowDelta1)
{
	backend->b.h->bgMode4active[params->backBufferIndex] = true;
	uint8_t* renderTargetPal = backend->b.h->bgMode4pal[params->backBufferIndex];

	uint32_t const r5  = cpu->gprs[5];  // height
	uint32_t const r7  = cpu->gprs[7];  // x1
	uint32_t const r8  = cpu->gprs[8];  // x0
	uint32_t const r10 = cpu->gprs[10]; // destination vram pointer
	uint32_t const texPtr = cpu->gprs[11];

	uint16_t const x0 = (r8 >> 16);
	uint16_t const x1 = (r7 >> 16);
	int16_t const dx0 = (r8 & 0xffff);
	int16_t const dx1 = (r7 & 0xffff);

	int const top = ((r10-0x06000000)%0xa000)/GBA_VIDEO_HORIZONTAL_PIXELS;

	uint32_t const uv0 = cpu->memory.load32(cpu, addrUv0, NULL);
	uint32_t const uv1 = cpu->memory.load32(cpu, addrUv1, NULL);
	uint32_t const uvRowDelta0 = cpu->memory.load32(cpu, addrUvRowDelta0, NULL);
	uint32_t const uvRowDelta1 = cpu->memory.load32(cpu, addrUvRowDelta1, NULL);

	uint16_t const u0 = (uv0 >> 16);
	uint16_t const u1 = (uv1 >> 16);
	uint16_t const v0 = (uv0 & 0xffff);
	uint16_t const v1 = (uv1 & 0xffff);
	int16_t const uRowDelta0 = (uvRowDelta0 >> 16);
	int16_t const uRowDelta1 = (uvRowDelta1 >> 16);
	int16_t const vRowDelta0 = (uvRowDelta0 & 0xffff);
	int16_t const vRowDelta1 = (uvRowDelta1 & 0xffff);

	for (int y = 0; y < (int)r5*params->scale; ++y) {
		int const left  = (x0*params->scale+(dx0*y)) >> 8;
		int const right = (x1*params->scale+(dx1*y)) >> 8;

		int /*uint32_t*/ const w = right-left;

		int /*int16_t*/ const uLeft  = u0+(uRowDelta0*y)/params->scale;
		int /*int16_t*/ const uRight = u1+(uRowDelta1*y)/params->scale;
		int /*int16_t*/ const vLeft  = v0+(vRowDelta0*y)/params->scale;
		int /*int16_t*/ const vRight = v1+(vRowDelta1*y)/params->scale;

		for (int x = left; x < right; ++x) {
			uint16_t const u = uLeft+((uRight-uLeft)*(x-left))/w;
			uint16_t const v = vLeft+((vRight-vLeft)*(x-left))/w;
			renderTargetPal[(top*params->scale+y)*params->rtWidth+x] = cpu->memory.load8(cpu, texPtr+(v&0xff00)+(u>>8), NULL);
		}
	}
}



static void DrawAsterixPlayerSprite(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params, uint8_t paletteMask)
{
	backend->b.h->bgMode4active[params->backBufferIndex] = true;
	uint8_t* renderTargetPal = backend->b.h->bgMode4pal[params->backBufferIndex];

	uint32_t const r11 = cpu->gprs[11];
	uint32_t spriteInfoPtr = cpu->memory.load32(cpu, r11, NULL);
	bool const mirror = (spriteInfoPtr & 0x80000000u) != 0;
	spriteInfoPtr &= 0x7fffffffu;
	int32_t const baseY = cpu->memory.load32(cpu, r11+4, NULL);
	int32_t const baseX = cpu->memory.load32(cpu, r11+8, NULL);
	int8_t const offsetX = cpu->memory.load8(cpu, spriteInfoPtr+0, NULL);
	int8_t const offsetY = cpu->memory.load8(cpu, spriteInfoPtr+1, NULL);
	uint8_t const width  = cpu->memory.load8(cpu, spriteInfoPtr+2, NULL);
	uint8_t const height = cpu->memory.load8(cpu, spriteInfoPtr+3, NULL);
	uint32_t const spritePtr = cpu->memory.load32(cpu, spriteInfoPtr+4, NULL);

	if (!mirror) {
		if (kDebugDraw) {
			HLE3DDebugDrawRect(backend->b.h, (baseX + offsetX), (baseY + offsetY), width, height, 0xff0000);
		}

		for (int y = 0; y < height; ++y) {
			int const gy = baseY+offsetY+y;
			if (gy >= 0 && gy < GBA_VIDEO_VERTICAL_PIXELS) {
				for (int x = 0; x < width; ++x) {
					int const gx = baseX+offsetX+x;
					if (gx >= 0 && gx < GBA_VIDEO_HORIZONTAL_PIXELS) {
						uint8_t const idx = (cpu->memory.load8(cpu, spritePtr + ((y*width+x)/2), NULL) >> ((x%2)?0:4)) & 0xf;
						if (idx != 0) {
							for (int sy = 0; sy < params->scale; ++sy) {
								int const cy = gy*params->scale+sy;
								for (int sx = 0; sx < params->scale; ++sx) {
									int const cx = gx*params->scale+sx;
									renderTargetPal[cy*params->rtWidth+cx] = idx | paletteMask;
								}
							}
						}
					}
				}
			}
		}
	} else {
		if (kDebugDraw) {
			HLE3DDebugDrawRect(backend->b.h, baseX-offsetX-width, baseY+offsetY, width, height, 0xff0000);
		}

		for (int y = 0; y < height; ++y) {
			int const gy = baseY+offsetY+y;
			if (gy >= 0 && gy < GBA_VIDEO_VERTICAL_PIXELS) {
				for (int x = 0; x < width; ++x) {
					int const gx = baseX-offsetX-width+x;
					if (gx >= 0 && gx < GBA_VIDEO_HORIZONTAL_PIXELS) {
						uint8_t const idx = (cpu->memory.load8(cpu, spritePtr + ((y*width+width-1-x)/2), NULL) >> ((x%2)?4:0)) & 0xf;
						if (idx != 0) {
							for (int sy = 0; sy < params->scale; ++sy) {
								int const cy = gy*params->scale+sy;
								for (int sx = 0; sx < params->scale; ++sx) {
									int const cx = gx*params->scale+sx;
									renderTargetPal[cy*params->rtWidth+cx] = idx | paletteMask;
								}
							}
						}
					}
				}
			}
		}
	}
}

static void DrawAsterixScaledEnvSprite(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params)
{
	backend->b.h->bgMode4active[params->backBufferIndex] = true;
	uint8_t* renderTargetPal = backend->b.h->bgMode4pal[params->backBufferIndex];

	uint32_t const r11 = cpu->gprs[11];

	uint32_t const texPtr = cpu->memory.load32(cpu, r11+0,  NULL); // r11
	int32_t  const y0     = cpu->memory.load32(cpu, r11+4,  NULL); // r0
	int32_t  const x0     = cpu->memory.load32(cpu, r11+8,  NULL); // r3
	uint32_t const v0     = cpu->memory.load32(cpu, r11+12, NULL); // r4
	uint32_t const u0     = cpu->memory.load32(cpu, r11+16, NULL); // r7
	int32_t  const y1     = cpu->memory.load32(cpu, r11+20, NULL); // r8
	int32_t  const x1     = cpu->memory.load32(cpu, r11+24, NULL); // r9
	uint32_t const v1     = cpu->memory.load32(cpu, r11+28, NULL); // r12
	uint32_t const u1     = cpu->memory.load32(cpu, r11+32, NULL); // lr

	if (kDebugDraw) {
		HLE3DDebugDrawRect(backend->b.h, x0, y0, (x1 - x0), (y1 - y0), 0x0000ff);
	}

	int const left   = x0*params->scale;
	int const right  = x1*params->scale;
	int const top    = y0*params->scale;
	int const bottom = y1*params->scale;

	for (int y = top; y < bottom; ++y) {
		if (y >= 0 && y < params->rtHeight) {
			int const v = v0 + ((y-top)*(v1-v0))/(bottom-top);
			for (int x = left; x < right; ++x) {
				if (x >= 0 && x < params->rtWidth) {
					int const u = u0 + ((x-left)*(u1-u0))/(right-left);
					uint8_t const texel = cpu->memory.load8(cpu, texPtr+v*256+u, NULL);
					if (texel != 0) {
						renderTargetPal[y*params->rtWidth+x] = texel;
					}
				}
			}
		}
	}
}

static void DrawAsterixScaledNpcSprite(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params)
{
	backend->b.h->bgMode4active[params->backBufferIndex] = true;
	uint8_t* renderTargetPal = backend->b.h->bgMode4pal[params->backBufferIndex];

	uint32_t const r11 = cpu->gprs[11];

	uint32_t const spriteStride = cpu->memory.load32(cpu, r11+0,  NULL); // r0
	uint32_t const palette      = cpu->memory.load32(cpu, r11+4,  NULL); // r1
	uint32_t const texPtr       = cpu->memory.load32(cpu, r11+8,  NULL); // r2
	int32_t  const y0           = cpu->memory.load32(cpu, r11+12, NULL); // r3
	int32_t  const x0           = cpu->memory.load32(cpu, r11+16, NULL); // r4
	uint32_t const v0           = cpu->memory.load32(cpu, r11+20, NULL); // r5
	uint32_t const u0           = cpu->memory.load32(cpu, r11+24, NULL); // r6
	int32_t  const y1           = cpu->memory.load32(cpu, r11+28, NULL); // r7
	int32_t  const x1           = cpu->memory.load32(cpu, r11+32, NULL); // r8
	uint32_t const v1           = cpu->memory.load32(cpu, r11+36, NULL); // r9
	uint32_t const u1           = cpu->memory.load32(cpu, r11+40, NULL); // r10

	if (kDebugDraw) {
		HLE3DDebugDrawRect(backend->b.h, x0, y0, (x1 - x0), (y1 - y0), 0x00ff00);
	}

	int const left   = x0*params->scale;
	int const right  = x1*params->scale;
	int const top    = y0*params->scale;
	int const bottom = y1*params->scale;
	uint8_t const paletteOverlay = (palette << 4);

	for (int y = top; y < bottom; ++y) {
		if (y >= 0 && y < params->rtHeight) {
			int const v = v0 + ((y-top)*(v1-v0))/(bottom-top);
			for (int x = left; x < right; ++x) {
				if (x >= 0 && x < params->rtWidth) {
					int const u = u0 + ((x-left)*(u1-u0))/(right-left);
					uint8_t const texel = (cpu->memory.load8(cpu, texPtr+(v*spriteStride)+(u/2), NULL) >> ((u%2)?0:4)) & 0xf;
					if (texel != 0) {
						renderTargetPal[y*params->rtWidth+x] = texel | paletteOverlay;
					}
				}
			}
		}
	}
}



static void DrawDriv3rPlayerSprite(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params)
{
	backend->b.h->bgMode4active[params->backBufferIndex] = true;
	uint8_t* renderTargetPal = backend->b.h->bgMode4pal[params->backBufferIndex];

	uint32_t const r11 = cpu->gprs[11];
	uint32_t spriteInfoPtr = cpu->memory.load32(cpu, r11, NULL);
	bool const mirror = (spriteInfoPtr & 0x80000000u) != 0;
	spriteInfoPtr &= 0x7fffffffu;

	// these are swapped from Asterix for bonus marks :)))
	int32_t const baseX = cpu->memory.load32(cpu, r11+4, NULL);
	int32_t const baseY = cpu->memory.load32(cpu, r11+8, NULL);

	int8_t const offsetX = cpu->memory.load8(cpu, spriteInfoPtr+0, NULL);
	int8_t const offsetY = cpu->memory.load8(cpu, spriteInfoPtr+1, NULL);
	uint8_t const width  = cpu->memory.load8(cpu, spriteInfoPtr+2, NULL);
	uint8_t const height = cpu->memory.load8(cpu, spriteInfoPtr+3, NULL);
	uint32_t const spritePtr = cpu->memory.load32(cpu, spriteInfoPtr+4, NULL);

	if (!mirror) {
		if (kDebugDraw) {
			HLE3DDebugDrawRect(backend->b.h, (baseX + offsetX), (baseY + offsetY), width, height, 0xff0000);
		}

		for (int y =0 ; y < height; ++y) {
			int const gy = baseY+offsetY+y;
			if (gy >= 0 && gy < GBA_VIDEO_VERTICAL_PIXELS) {
				for (int x =0 ; x < width; ++x) {
					int const gx = baseX+offsetX+x;
					if (gx >= 0 && gx < GBA_VIDEO_HORIZONTAL_PIXELS) {
						uint8_t const idx = cpu->memory.load8(cpu, spritePtr + (y*width+x), NULL);
						if (idx != 0) {
							for (int sy = 0; sy < params->scale; ++sy) {
								int const cy = gy*params->scale+sy;
								for (int sx = 0; sx < params->scale; ++sx) {
									int const cx = gx*params->scale+sx;
									renderTargetPal[cy*params->rtWidth+cx] = idx;
								}
							}
						}
					}
				}
			}
		}
	} else {
		if (kDebugDraw) {
			HLE3DDebugDrawRect(backend->b.h, (baseX - offsetX - width), (baseY + offsetY), width, height, 0xff0000);
		}

		for (int y = 0; y < height; ++y) {
			int const gy = baseY+offsetY+y;
			if (gy >= 0 && gy < GBA_VIDEO_VERTICAL_PIXELS) {
				for (int x = 0; x < width; ++x) {
					int const gx = baseX-offsetX-width+x;
					if (gx >= 0 && gx < GBA_VIDEO_HORIZONTAL_PIXELS) {
						uint8_t const idx = cpu->memory.load8(cpu, spritePtr + (y*width+width-1-x), NULL);
						if (idx != 0) {
							for (int sy = 0; sy < params->scale; ++sy) {
								int const cy = gy*params->scale+sy;
								for (int sx = 0; sx < params->scale; ++sx) {
									int const cx = gx*params->scale+sx;
									renderTargetPal[cy*params->rtWidth+cx] = idx;
								}
							}
						}
					}
				}
			}
		}
	}
}

static void DrawDriv3rScaledSprite(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params)
{
	backend->b.h->bgMode4active[params->backBufferIndex] = true;
	uint8_t* renderTargetPal = backend->b.h->bgMode4pal[params->backBufferIndex];

	uint32_t const r11 = cpu->gprs[11];

	uint32_t const texPtr       = cpu->memory.load32(cpu, r11+0,  NULL); // r0
	int32_t  const x0           = cpu->memory.load32(cpu, r11+4,  NULL); // r1
	int32_t  const y0           = cpu->memory.load32(cpu, r11+8,  NULL); // r2
	int32_t  const u0           = cpu->memory.load32(cpu, r11+12, NULL); // r3
	int32_t  const v0           = cpu->memory.load32(cpu, r11+16, NULL); // r4
	int32_t  const x1           = cpu->memory.load32(cpu, r11+20, NULL); // r5
	int32_t  const y1           = cpu->memory.load32(cpu, r11+24, NULL); // r6
	int32_t  const u1           = cpu->memory.load32(cpu, r11+28, NULL); // r7
	int32_t  const v1           = cpu->memory.load32(cpu, r11+32, NULL); // r8
	int32_t  const spriteStride = cpu->memory.load32(cpu, r11+36, NULL); // r9

	if (kDebugDraw) {
		HLE3DDebugDrawRect(backend->b.h, x0, y0, (x1 - x0), (y1 - y0), 0x0000ff);
	}

	int const left   = x0*params->scale;
	int const right  = x1*params->scale;
	int const top    = y0*params->scale;
	int const bottom = y1*params->scale;

	for (int y = top; y < bottom; ++y) {
		if (y >= 0 && y < params->rtHeight) {
			for (int x = left; x < right; ++x) {
				if (x >= 0 && x < params->rtWidth) {
					int const u = u0 + ((x-left)*(u1-u0))/(right-left);
					int const v = v0 + ((y-top)*(v1-v0))/(bottom-top);
					uint8_t const texel = cpu->memory.load8(cpu, texPtr+v*spriteStride+u, NULL);
					if (texel != 0) {
						renderTargetPal[y*params->rtWidth+x] = texel;
					}
				}
			}
		}
	}
}
