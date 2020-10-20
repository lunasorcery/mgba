/*
High-Level Emulation of 3D engine from:
- Asterix & Obelix XXL

Original renderer by Fernando Velez & Guillaume Dubail
Emulation written by @lunasorcery
*/
#include <mgba/internal/arm/arm.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/hle3d/hle3d.h>
#include <mgba/hle3d/backends/asterix.h>

static uint32_t const
	kIdentAsterixXXL     = 0x50584c42, // BLXP
	kIdentAsterixXXL2in1 = 0x50413242; // B2AP

static bool const kDebugDraw = false;

static void ClearScreen(struct HLE3DBackend* backend, struct ARMCore* cpu);
static void FlipBuffers(struct HLE3DBackend* backend, struct ARMCore* cpu);
static void FillColoredTrapezoid(struct HLE3DBackend* backend, struct ARMCore* cpu);
static void FillTexturedTrapezoid(struct HLE3DBackend* backend, struct ARMCore* cpu);
static void Draw(struct HLE3DBackend* backend, struct ARMCore* cpu);
static void DrawPlayerSprite(struct HLE3DBackend* backend, struct ARMCore* cpu, uint8_t* renderTarget, int scale, uint8_t paletteMask);
static void DrawEnvSprite(struct HLE3DBackend* backend, struct ARMCore* cpu, uint8_t* renderTarget, int scale);
static void DrawNpcSprite(struct HLE3DBackend* backend, struct ARMCore* cpu, uint8_t* renderTarget, int scale);

void HLE3DBackendAsterixCreate(struct HLE3DBackendAsterix* backend)
{
	backend->b.init   = HLE3DBackendAsterixInit;
	backend->b.deinit = HLE3DBackendAsterixDeinit;
	backend->b.isGame = HLE3DBackendAsterixIsGame;
	backend->b.hook   = HLE3DBackendAsterixHook;
}

void HLE3DBackendAsterixInit(struct HLE3DBackend* backend, struct HLE3D* hle3d, uint32_t ident)
{
	UNUSED(backend);

	if (ident == kIdentAsterixXXL || ident == kIdentAsterixXXL2in1)
	{
		HLE3DAddBreakpoint(hle3d, 0x03004198); // clear screen
		HLE3DAddBreakpoint(hle3d, 0x03004300); // switch based on prim type
		HLE3DAddBreakpoint(hle3d, 0x030044e8); // colored quad begin segment
		HLE3DAddBreakpoint(hle3d, 0x0300494c); // textured quad begin segment (a couple of instructions early to catch both types)
		HLE3DAddBreakpoint(hle3d, 0x030075B8); // flip buffers
	}
}

void HLE3DBackendAsterixDeinit(struct HLE3DBackend* backend)
{
	UNUSED(backend);
}

bool HLE3DBackendAsterixIsGame(uint32_t ident)
{
	return
		(ident == kIdentAsterixXXL) ||
		(ident == kIdentAsterixXXL2in1);
}

void HLE3DBackendAsterixHook(struct HLE3DBackend* backend, struct ARMCore* cpu, uint32_t pc)
{
	// clear screen ahead of rendering
	if (pc == 0x03004198) {
		ClearScreen(backend, cpu);
		return;
	}

	// flip buffers after rendering
	if (pc == 0x030075B8) {
		FlipBuffers(backend, cpu);
		return;
	}

	// draw something
	if (pc == 0x03004300) {
		Draw(backend, cpu);
		return;
	}

	// color fill
	if (pc == 0x030044e8) {
		FillColoredTrapezoid(backend, cpu);
		return;
	}

	// texture fill
	if (pc == 0x0300494c) {
		FillTexturedTrapezoid(backend, cpu);
		return;
	}
}

static void ClearScreen(struct HLE3DBackend* backend, struct ARMCore* cpu)
{
	//printf("[HLE3D/AsterixXXL] ---- clear screen ----\n");
	uint8_t const frame = cpu->memory.load8(cpu, 0x0203DC1B, NULL);
	backend->h->bgMode4active[1-frame] = true;

	int const scale = backend->h->renderScale;
	memset(backend->h->bgMode4pal[1-frame], 0, 240*160*scale*scale);
}

static void FlipBuffers(struct HLE3DBackend* backend, struct ARMCore* cpu)
{
	//printf("[HLE3D/AsterixXXL] ---- flip buffers ----\n");
	uint16_t const value = cpu->gprs[2];
	uint8_t const mode = (value & 0x7);
	if (mode == 4) {
		uint8_t const frame = (value >> 4) & 1;
		backend->h->bgMode4active[1-frame] = false;
		HLE3DCommitMode4Buffer(backend->h, cpu, frame);
	} else {
		backend->h->bgMode4active[0] = false;
		backend->h->bgMode4active[1] = false;
	}
}

static void FillColoredTrapezoid(struct HLE3DBackend* backend, struct ARMCore* cpu)
{
	uint32_t const r5 = cpu->gprs[5];
	uint32_t const r7 = cpu->gprs[7];
	uint32_t const r8 = cpu->gprs[8];
	uint32_t const r10 = cpu->gprs[10];

	uint8_t const frame = cpu->memory.load8(cpu, 0x0203DC1B, NULL);
	uint8_t* renderTargetPal = backend->h->bgMode4pal[1-frame];
	int const scale = backend->h->renderScale;
	int const stride = 240*scale;

	uint8_t const color = cpu->memory.load8(cpu, 0x03004708, NULL);

	uint16_t const x0 = (r8 >> 16);
	uint16_t const x1 = (r7 >> 16);
	int16_t const dx0 = (r8 & 0xffff);
	int16_t const dx1 = (r7 & 0xffff);

	int const top = ((r10-0x06000000)%0xa000)/240;

	for (int y = 0; y < (int)r5*scale; ++y) {
		int const left = (x0*scale+(dx0*y)) >> 8;
		int const right = (x1*scale+(dx1*y)) >> 8;

		if (right > left) {
			memset(&renderTargetPal[(top*scale+y)*stride+left], color, right-left);
		}
	}
}

static void FillTexturedTrapezoid(struct HLE3DBackend* backend, struct ARMCore* cpu)
{
	uint32_t const r5 = cpu->gprs[5];
	uint32_t const r7 = cpu->gprs[7];
	uint32_t const r8 = cpu->gprs[8];
	uint32_t const r10 = cpu->gprs[10];
	uint32_t const texPtr = cpu->gprs[11];

	uint8_t const frame = cpu->memory.load8(cpu, 0x0203DC1B, NULL);
	uint8_t* renderTargetPal = backend->h->bgMode4pal[1-frame];
	int const scale = backend->h->renderScale;
	int const stride = 240*scale;

	uint16_t const x0 = (r8 >> 16);
	uint16_t const x1 = (r7 >> 16);
	int16_t const dx0 = (r8 & 0xffff);
	int16_t const dx1 = (r7 & 0xffff);

	int const top = ((r10-0x06000000)%0xa000)/240;

	uint32_t const uv0 = cpu->gprs[0];
	uint32_t const uv1 = cpu->gprs[1];
	uint32_t const uvRowDelta0 = cpu->memory.load32(cpu, 0x0300472c, NULL);
	uint32_t const uvRowDelta1 = cpu->memory.load32(cpu, 0x03004730, NULL);

	uint16_t const u0 = (uv0 >> 16);
	uint16_t const u1 = (uv1 >> 16);
	uint16_t const v0 = (uv0 & 0xffff);
	uint16_t const v1 = (uv1 & 0xffff);
	int16_t const uRowDelta0 = (uvRowDelta0 >> 16);
	int16_t const uRowDelta1 = (uvRowDelta1 >> 16);
	int16_t const vRowDelta0 = (uvRowDelta0 & 0xffff);
	int16_t const vRowDelta1 = (uvRowDelta1 & 0xffff);

	for (int y = 0; y < (int)r5*scale; ++y) {
		int const left = (x0*scale+(dx0*y)) >> 8;
		int const right = (x1*scale+(dx1*y)) >> 8;

		int /*uint32_t*/ const w = right-left;

		int /*int16_t*/ const uLeft = u0+(uRowDelta0*y)/scale;
		int /*int16_t*/ const uRight = u1+(uRowDelta1*y)/scale;
		int /*int16_t*/ const vLeft = v0+(vRowDelta0*y)/scale;
		int /*int16_t*/ const vRight = v1+(vRowDelta1*y)/scale;

		for (int x = left; x < right; ++x) {
			uint16_t const u = uLeft+((uRight-uLeft)*(x-left))/w;
			uint16_t const v = vLeft+((vRight-vLeft)*(x-left))/w;
			renderTargetPal[(top*scale+y)*stride+x] = cpu->memory.load8(cpu, texPtr+(v&0xff00)+(u>>8), NULL);
		}
	}
}

static void Draw(struct HLE3DBackend* backend, struct ARMCore* cpu)
{
	uint32_t const drawType = cpu->gprs[0];

	// early-out for types we handle elsewhere
	switch (drawType) {
		case 0: // abort
		case 1: // colored fill, we'll catch this with a per-segment hook
		case 2: // link to next draw, which we'll catch with a hook
		case 3: // 1px textured fill, we'll catch this with a per-segment hook
		case 4: // 2px textured fill, we'll catch this with a per-segment hook
			return;
	}

	uint8_t const frame = cpu->memory.load8(cpu, 0x0203DC1B, NULL);
	uint8_t* renderTargetPal = backend->h->bgMode4pal[1-frame];
	int const scale = backend->h->renderScale;

	switch (drawType) {
		case 5:
			DrawPlayerSprite(backend, cpu, renderTargetPal, scale, 0x00);
			break;
		case 6:
			DrawPlayerSprite(backend, cpu, renderTargetPal, scale, 0x10);
			break;
		case 7:
			DrawEnvSprite(backend, cpu, renderTargetPal, scale);
			break;
		case 8:
			DrawNpcSprite(backend, cpu, renderTargetPal, scale);
			break;
	}
}

static void DrawPlayerSprite(struct HLE3DBackend* backend, struct ARMCore* cpu, uint8_t* renderTarget, int scale, uint8_t paletteMask)
{
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
			HLE3DDebugDrawRect(backend->h, baseX+offsetX, baseY+offsetY, width, height, 0xff0000);
		}

		for (int y=0;y<height;++y) {
			for (int x=0;x<width;++x) {
				int const gx = baseX+offsetX+x;
				int const gy = baseY+offsetY+y;
				if (gx >= 0 && gx < 240 && gy >= 0 && gy < 160) {
					uint8_t const idx = (cpu->memory.load8(cpu, spritePtr + ((y*width+x)/2), NULL) >> ((x%2)?0:4)) & 0xf;
					if (idx != 0) {
						for (int sy=0;sy<scale;++sy) {
							for (int sx=0;sx<scale;++sx) {
								int const cx = gx*scale+sx;
								int const cy = gy*scale+sy;
								renderTarget[cy*240*scale+cx] = idx | paletteMask;
							}
						}
					}
				}
			}
		}
	} else {
		if (kDebugDraw) {
			HLE3DDebugDrawRect(backend->h, baseX-offsetX-width, baseY+offsetY, width, height, 0xff0000);
		}

		for (int y=0;y<height;++y) {
			for (int x=0;x<width;++x) {
				int const gx = baseX-offsetX-width+x;
				int const gy = baseY+offsetY+y;
				if (gx >= 0 && gx < 240 && gy >= 0 && gy < 160) {
					uint8_t const idx = (cpu->memory.load8(cpu, spritePtr + ((y*width+width-1-x)/2), NULL) >> ((x%2)?4:0)) & 0xf;
					if (idx != 0) {
						for (int sy=0;sy<scale;++sy) {
							for (int sx=0;sx<scale;++sx) {
								int const cx = gx*scale+sx;
								int const cy = gy*scale+sy;
								renderTarget[cy*240*scale+cx] = idx | paletteMask;
							}
						}
					}
				}
			}
		}
	}
}

static void DrawEnvSprite(struct HLE3DBackend* backend, struct ARMCore* cpu, uint8_t* renderTarget, int scale)
{
	uint32_t const r11 = cpu->gprs[11];

	int32_t const y0 = cpu->memory.load32(cpu, r11+4, NULL);
	int32_t const x0 = cpu->memory.load32(cpu, r11+8, NULL);
	int32_t const y1 = cpu->memory.load32(cpu, r11+20, NULL);
	int32_t const x1 = cpu->memory.load32(cpu, r11+24, NULL);

	uint32_t const v0 = cpu->memory.load32(cpu, r11+12, NULL);
	uint32_t const u0 = cpu->memory.load32(cpu, r11+16, NULL);
	uint32_t const v1 = cpu->memory.load32(cpu, r11+28, NULL);
	uint32_t const u1 = cpu->memory.load32(cpu, r11+32, NULL);
	uint32_t const texPtr = cpu->memory.load32(cpu, r11+0, NULL);

	if (kDebugDraw) {
		int16_t const height = y1 - y0;
		int16_t const width = x1 - x0;
		HLE3DDebugDrawRect(backend->h, x0, y0, width, height, 0x0000ff);
	}

	int const left = x0*scale;
	int const right = x1*scale;
	int const top = y0*scale;
	int const bottom = y1*scale;
	int const stride = 240*scale;

	for (int y=top;y<bottom;++y) {
		if (y >= 0 && y < 160*scale) {
			for (int x=left;x<right;++x) {
				if (x >= 0 && x < 240*scale) {
					int const u = u0 + ((x-left)*(u1-u0))/(right-left);
					int const v = v0 + ((y-top)*(v1-v0))/(bottom-top);
					uint8_t const texel = cpu->memory.load8(cpu, texPtr+v*256+u, NULL);
					if (texel != 0) {
						renderTarget[y*stride+x] = texel;
					}
				}
			}
		}
	}
}

static void DrawNpcSprite(struct HLE3DBackend* backend, struct ARMCore* cpu, uint8_t* renderTarget, int scale)
{
	uint32_t const r11 = cpu->gprs[11];

	int32_t const y0 = cpu->memory.load32(cpu, r11+12, NULL);
	int32_t const x0 = cpu->memory.load32(cpu, r11+16, NULL);
	int32_t const y1 = cpu->memory.load32(cpu, r11+28, NULL);
	int32_t const x1 = cpu->memory.load32(cpu, r11+32, NULL);

	uint32_t const spriteStride = cpu->memory.load32(cpu, r11+0, NULL);
	uint32_t const palette = cpu->memory.load32(cpu, r11+4, NULL);
	uint32_t const texPtr = cpu->memory.load32(cpu, r11+8, NULL);
	uint32_t const v0 = cpu->memory.load32(cpu, r11+20, NULL);
	uint32_t const u0 = cpu->memory.load32(cpu, r11+24, NULL);
	uint32_t const v1 = cpu->memory.load32(cpu, r11+36, NULL);
	uint32_t const u1 = cpu->memory.load32(cpu, r11+40, NULL);

	if (kDebugDraw) {
		int16_t const height = y1 - y0;
		int16_t const width = x1 - x0;
		HLE3DDebugDrawRect(backend->h, x0, y0, width, height, 0x00ff00);
	}

	int const left = x0*scale;
	int const right = x1*scale;
	int const top = y0*scale;
	int const bottom = y1*scale;
	int const rtStride = 240*scale;
	uint8_t const paletteOverlay = (palette << 4);

	for (int y=top;y<bottom;++y) {
		if (y >= 0 && y < 160*scale) {
			for (int x=left;x<right;++x) {
				if (x >= 0 && x < 240*scale) {
					int const u = u0 + ((x-left)*(u1-u0))/(right-left);
					int const v = v0 + ((y-top)*(v1-v0))/(bottom-top);
					uint8_t const texel = (cpu->memory.load8(cpu, texPtr+(v*spriteStride)+(u/2), NULL) >> ((u%2)?0:4)) & 0xf;
					if (texel != 0) {
						renderTarget[y*rtStride+x] = texel | paletteOverlay;
					}
				}
			}
		}
	}
}
