/*
High-Level Emulation of 3D engine from:
- Drome Racers
- Hot Wheels: Stunt Track Challenge

Original renderer written by Jason McGann
Emulation written by @lunasorcery
*/
#include <mgba/internal/arm/arm.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/hle3d/hle3d.h>
#include <mgba/hle3d/backends/drome.h>

static uint32_t const
	kIdentDromeEU             = 0x58454f41, // AOEX
	kIdentDromeNA             = 0x45454f41, // AOEE
	kIdentHotWheelsStuntTrack = 0x45454842, // BHEE
	kIdentHotWheels2Pack      = 0x454a5142; // BQJE

static uint8_t const
	kClipFlagRight  = 0x04,
	kClipFlagLeft   = 0x08,
	kClipFlagBottom = 0x10,
	kClipFlagTop    = 0x20;

struct RenderParams {
	// passed in via r0
	uint16_t viewportX;
	uint16_t viewportY;
	uint16_t viewportWidth;
	uint16_t viewportHeight;
	uint32_t baseTexPtr;
	uint32_t ptrSpriteParamsTable;

	// computed and stored on the stack
	uint16_t clipLeft;
	uint16_t clipTop;
	uint16_t clipRight;
	uint16_t clipBottom;
};

struct MultiPtr {
	union {
		int8_t* i8;
		int16_t* i16;
		int32_t* i32;
		uint8_t* u8;
		uint16_t* u16;
		uint32_t* u32;
	};
};

static void HookTransform(struct HLE3DBackendDrome* backend, struct ARMCore* cpu);
static void HookRasterizer(struct HLE3DBackendDrome* backend, struct ARMCore* cpu);

static void RasterizeColoredTri(struct HLE3DBackendDrome* backend, struct ARMCore* cpu, struct RenderParams const* params, uint8_t* renderTarget, uint32_t activeTriPtr);
static void RasterizeStaticTexTri(struct HLE3DBackendDrome* backend, struct ARMCore* cpu, struct RenderParams const* params, uint8_t* renderTarget, uint32_t activeTriPtr);
static void RasterizeAffineTexTri(struct HLE3DBackendDrome* backend, struct ARMCore* cpu, struct RenderParams const* params, uint8_t* renderTarget, uint32_t activeTriPtr);
static void RasterizeColoredTriClipped(struct HLE3DBackendDrome* backend, struct ARMCore* cpu, struct RenderParams const* params, uint8_t* renderTarget, uint32_t activeTriPtr);
static void RasterizeStaticTexTriClipped(struct HLE3DBackendDrome* backend, struct ARMCore* cpu, struct RenderParams const* params, uint8_t* renderTarget, uint32_t activeTriPtr);
static void RasterizeAffineTexTriClipped(struct HLE3DBackendDrome* backend, struct ARMCore* cpu, struct RenderParams const* params, uint8_t* renderTarget, uint32_t activeTriPtr);
static void RasterizeSpriteOccluder(struct HLE3DBackendDrome* backend, struct ARMCore* cpu, struct RenderParams const* params, uint8_t* renderTarget, uint32_t activeTriPtr);

static void FinalizeSpriteOccluders(struct HLE3DBackendDrome* backend, struct ARMCore* cpu, struct RenderParams const* params, uint8_t* renderTarget);

static void FillColoredTri(uint8_t* renderTarget, int scale, int32_t yx0, int32_t yx1, int32_t yx2, uint8_t colorIndex);
static void FillAffineTexTri(uint8_t* renderTarget, int scale, int32_t yx0, int32_t yx1, int32_t yx2, uint32_t vu0, uint32_t vu1, uint32_t vu2, uint32_t texPtr, struct ARMCore* cpu);

static void FillColoredTrapezoid(uint8_t* renderTarget, int scale, int32_t left, int32_t leftDelta, int32_t right, int32_t rightDelta, int32_t top, int32_t height, uint8_t color);
static void FillAffineTexTrapezoid(uint8_t* renderTarget, int scale, int32_t left, int32_t leftDelta, int32_t right, int32_t rightDelta, int32_t top, int32_t height, uint32_t uv, int32_t uvRowDelta, int32_t uvPixelDelta, struct ARMCore* cpu, uint32_t texPtr);

void HLE3DBackendDromeCreate(struct HLE3DBackendDrome* backend)
{
	backend->b.init   = HLE3DBackendDromeInit;
	backend->b.deinit = HLE3DBackendDromeDeinit;
	backend->b.isGame = HLE3DBackendDromeIsGame;
	backend->b.hook   = HLE3DBackendDromeHook;
}

void HLE3DBackendDromeInit(struct HLE3DBackend* backend, struct HLE3D* hle3d, uint32_t ident)
{
	struct HLE3DBackendDrome* dromeBackend = (struct HLE3DBackendDrome*)backend;

	dromeBackend->shouldHookTransform = false;
	dromeBackend->shouldHookRasterize = true;

	dromeBackend->disableRealTransform = false;
	dromeBackend->disableRealRasterizer = true;

	if (ident == kIdentDromeEU || ident == kIdentDromeNA)
	{
		dromeBackend->addrRamExecutionPoint    = 0x030023d4;
		dromeBackend->addrRamActiveFunctionPtr = 0x03000e04;
		dromeBackend->addrRomTransformFunc     = 0x08064880;
		dromeBackend->addrRomRasterizeFunc     = 0x08000330;
	}
	else if (ident == kIdentHotWheelsStuntTrack)
	{
		dromeBackend->addrRamExecutionPoint    = 0x0300243c;
		dromeBackend->addrRamActiveFunctionPtr = 0x03000dd8;
		dromeBackend->addrRomTransformFunc     = 0x080ea350;
		dromeBackend->addrRomRasterizeFunc     = 0x08085e00;
	}
	else if (ident == kIdentHotWheels2Pack)
	{
		dromeBackend->addrRamExecutionPoint    = 0x03002294;
		dromeBackend->addrRamActiveFunctionPtr = 0x03000dd8;
		dromeBackend->addrRomTransformFunc     = 0x088ea350;
		dromeBackend->addrRomRasterizeFunc     = 0x08885e00;
	}

	dromeBackend->spriteStackHeight = 0;
	memset(dromeBackend->spriteStack, 0, sizeof(dromeBackend->spriteStack));

	HLE3DAddBreakpoint(hle3d, dromeBackend->addrRamExecutionPoint);
}

void HLE3DBackendDromeDeinit(struct HLE3DBackend* backend)
{
	UNUSED(backend);
}

bool HLE3DBackendDromeIsGame(uint32_t ident)
{
	return
		(ident == kIdentDromeEU) ||
		(ident == kIdentDromeNA) ||
		(ident == kIdentHotWheelsStuntTrack) ||
		(ident == kIdentHotWheels2Pack);
}

void HLE3DBackendDromeHook(struct HLE3DBackend* backend, struct ARMCore* cpu)
{
	struct HLE3DBackendDrome* dromeBackend = (struct HLE3DBackendDrome*)backend;

	uint32_t const activeRamFunctionPtr = cpu->memory.load32(cpu, dromeBackend->addrRamActiveFunctionPtr, NULL);

	if (activeRamFunctionPtr == dromeBackend->addrRomTransformFunc) {
		HookTransform(dromeBackend, cpu);
	}

	if (activeRamFunctionPtr == dromeBackend->addrRomRasterizeFunc) {
		HookRasterizer(dromeBackend, cpu);
	}
}

static void HookTransform(struct HLE3DBackendDrome* backend, struct ARMCore* cpu)
{
	uint32_t const r0 = cpu->gprs[0];

	// pointer to linked list of objects to draw
	uint32_t const objectsPtr = cpu->memory.load32(cpu, r0+40, NULL);

	int16_t const camMtx00 = cpu->memory.load16(cpu, r0+0, NULL);
	int16_t const camMtx10 = cpu->memory.load16(cpu, r0+2, NULL);
	int16_t const camMtx20 = cpu->memory.load16(cpu, r0+4, NULL);
	int16_t const camMtx01 = cpu->memory.load16(cpu, r0+6, NULL);
	int16_t const camMtx11 = cpu->memory.load16(cpu, r0+8, NULL);
	int16_t const camMtx21 = cpu->memory.load16(cpu, r0+10, NULL);
	int16_t const camMtx02 = cpu->memory.load16(cpu, r0+12, NULL);
	int16_t const camMtx12 = cpu->memory.load16(cpu, r0+14, NULL);
	int16_t const camMtx22 = cpu->memory.load16(cpu, r0+16, NULL);
	//printf(
	//	"camMtx: [%.2f,%.2f,%.2f] [%.2f,%.2f,%.2f] [%.2f,%.2f,%.2f]\n",
	//	camMtx00/4096.0f, camMtx10/4096.0f, camMtx20/4096.0f,
	//	camMtx01/4096.0f, camMtx11/4096.0f, camMtx21/4096.0f,
	//	camMtx02/4096.0f, camMtx12/4096.0f, camMtx22/4096.0f);
	UNUSED(camMtx00); UNUSED(camMtx10); UNUSED(camMtx20);
	UNUSED(camMtx01); UNUSED(camMtx11); UNUSED(camMtx21);
	UNUSED(camMtx02); UNUSED(camMtx12); UNUSED(camMtx22);

	int32_t const camPosX = cpu->memory.load32(cpu, r0+20, NULL);
	int32_t const camPosY = cpu->memory.load32(cpu, r0+24, NULL);
	int32_t const camPosZ = cpu->memory.load32(cpu, r0+28, NULL);
	//printf(
	//	"camPos: (%.2f,%.2f,%.2f)\n",
	//	camPosX/4096.0f,
	//	camPosY/4096.0f,
	//	camPosZ/4096.0f);
	UNUSED(camPosX);
	UNUSED(camPosY);
	UNUSED(camPosZ);


	uint32_t numObjects = 0;
	uint32_t activeObjectPtr = objectsPtr;
	while (activeObjectPtr != 0) {
		numObjects++;
		//uint32_t const modelPtr = cpu->memory.load32(cpu, activeObjectPtr+20, NULL);
		//int32_t const modelPosX = cpu->memory.load32(cpu, activeObjectPtr+24, NULL);
		//int32_t const modelPosY = cpu->memory.load32(cpu, activeObjectPtr+28, NULL);
		//int32_t const modelPosZ = cpu->memory.load32(cpu, activeObjectPtr+32, NULL);
		//printf(
		//	"model %08x @ (%.2f, %.2f, %.2f)\n",
		//	modelPtr,
		//	modelPosX/4096.0f,
		//	modelPosY/4096.0f,
		//	modelPosZ/4096.0f);
		activeObjectPtr = cpu->memory.load32(cpu, activeObjectPtr+36, NULL);
	}
	//printf("%d objects\n", numObjects);

	// disable the real transform pipeline
	// by making the objects pointer null
	if (backend->disableRealTransform) {
		cpu->memory.store32(cpu, r0+40, 0, NULL);
	}
}

static void HookRasterizer(struct HLE3DBackendDrome* backend, struct ARMCore* cpu)
{
	uint32_t const r0 = cpu->gprs[0];

	// pointer to the first primitive
	uint32_t const renderStreamPtr = cpu->memory.load32(cpu, r0+56, NULL);

	// destination vram bank
	uint32_t const drawBuffer = cpu->memory.load32(cpu, r0+60, NULL);

	struct RenderParams params;
	params.viewportX            = cpu->memory.load16(cpu, r0+32, NULL);
	params.viewportY            = cpu->memory.load16(cpu, r0+34, NULL);
	params.viewportWidth        = cpu->memory.load16(cpu, r0+36, NULL);
	params.viewportHeight       = cpu->memory.load16(cpu, r0+38, NULL);
	params.baseTexPtr           = cpu->memory.load32(cpu, r0+64, NULL);
	params.ptrSpriteParamsTable = r0+96;

	// deliberately *don't* increment by a half-pixel
	// because it causes issues with screen-edge clipping at high resolutions
	params.clipLeft   = (params.viewportX*8);//+4;
	params.clipTop    = (params.viewportY*8);//+4;
	params.clipRight  = params.clipLeft + (params.viewportWidth*8);
	params.clipBottom = params.clipTop + (params.viewportHeight*8);

	int const scale = backend->b.h->renderScale;
	int const activeFrameIndex = (drawBuffer == 0x06000000) ? 0 : 1;
	uint8_t* renderTargetPal = backend->b.h->bgMode4pal[activeFrameIndex];
	memset(renderTargetPal, 0, 240*160*scale*scale);

	uint32_t activeTriPtr = renderStreamPtr;
	uint8_t activeTriType = cpu->memory.load8(cpu, activeTriPtr, NULL);

	// kinda yucky hack for the textured background on the pause menu
	// since the pause menu has no primitives
	backend->b.h->bgMode4active = (activeTriType != 0);

	while (activeTriType != 0) {
		switch (activeTriType)
		{
			case 1:
				RasterizeColoredTri(backend, cpu, &params, renderTargetPal, activeTriPtr);
				break;
			case 2:
				RasterizeStaticTexTri(backend, cpu, &params, renderTargetPal, activeTriPtr);
				break;
			case 3:
				RasterizeAffineTexTri(backend, cpu, &params, renderTargetPal, activeTriPtr);
				break;
			case 4:
				RasterizeColoredTriClipped(backend, cpu, &params, renderTargetPal, activeTriPtr);
				break;
			case 5:
				RasterizeStaticTexTriClipped(backend, cpu, &params, renderTargetPal, activeTriPtr);
				break;
			case 6:
				RasterizeAffineTexTriClipped(backend, cpu, &params, renderTargetPal, activeTriPtr);
				break;
			case 7:
				RasterizeSpriteOccluder(backend, cpu, &params, renderTargetPal, activeTriPtr);
				break;
		}

		uint16_t const nextTriPtr = cpu->memory.load16(cpu, activeTriPtr+2, NULL);
		activeTriPtr = (activeTriPtr & 0xffff0000) | nextTriPtr;
		activeTriType = cpu->memory.load8(cpu, activeTriPtr, NULL);
	}

	if (backend->disableRealRasterizer) {
		// disable the real rasterizer
		// by making the first primitive the "end" primitive
		cpu->memory.store8(cpu, renderStreamPtr, 0, NULL);
	}

	FinalizeSpriteOccluders(backend, cpu, &params, renderTargetPal);

	uint8_t* renderTargetColor = backend->b.h->bgMode4color[activeFrameIndex];
	memset(renderTargetColor, 0, 240*160*scale*scale*4);
	uint8_t palette[256*3];
	for (int i = 0; i < 256; ++i) {
		uint16_t const color555 = cpu->memory.load16(cpu, 0x05000000+i*2, NULL);
		uint32_t const color888 = mColorFrom555(color555);
		uint8_t const r = (color888 & 0xff);
		uint8_t const g = ((color888 >> 8) & 0xff);
		uint8_t const b = ((color888 >> 16) & 0xff);
		palette[i*3+0] = r;
		palette[i*3+1] = g;
		palette[i*3+2] = b;
	}
	for (int i = 0; i < 240*160*scale*scale; ++i) {
		uint8_t const index = renderTargetPal[i];
		if (index != 0) {
			renderTargetColor[i*4+0] = palette[index*3+0];
			renderTargetColor[i*4+1] = palette[index*3+1];
			renderTargetColor[i*4+2] = palette[index*3+2];
			renderTargetColor[i*4+3] = 0xff;
		}
	}
}

#define XorSwap(a, b) { \
	if (a != b) { \
		*a ^= *b; \
		*b ^= *a; \
		*a ^= *b; \
	} \
}

#define DivTable(n) \
	(int32_t)((int32_t)0x40000000 / (int32_t)((n == 0) ? 1 : n))

#define smlal(RdLo, RdHi, Rn, Rm) { \
	int64_t const t = (int64_t)(Rn) * (int64_t)(Rm); \
	(RdHi) += (int32_t)(t >> 32); \
	(RdLo) += (int32_t)(t & 0xffffffffu); \
}

static void FillColoredTrapezoid(
	uint8_t* renderTarget, int scale,
	int32_t left, int32_t leftDelta,
	int32_t right, int32_t rightDelta,
	int32_t top, int32_t height,
	uint8_t color)
{
	int const stride = 240*scale;
	uint8_t* rowPtr = renderTarget+stride*top;
	while (height--) {
		int const l = left >> 16;
		int const r = right >> 16;
		int const w = r-l;
		if (w > 0) {
			memset(rowPtr+l, color, w);
		}
		left += leftDelta;
		right += rightDelta;
		rowPtr += stride;
	}
}

static void FillAffineTexTrapezoid(
	uint8_t* renderTarget, int scale,
	int32_t left, int32_t leftDelta,
	int32_t right, int32_t rightDelta,
	int32_t top, int32_t height,
	uint32_t uv, int32_t uvRowDelta, int32_t uvPixelDelta,
	struct ARMCore* cpu, uint32_t texPtr)
{
	// this is kinda yucky
	// but it does mean we can read the texture faster
	struct GBA* gba = (struct GBA*)cpu->master;
	struct GBAMemory* memory = &gba->memory;
	uint8_t const* rawTexPtr = ((uint8_t*)memory->rom) + (texPtr&0xffffff);

	int const stride = 240*scale;
	uint8_t* rowPtr = renderTarget+stride*top;
	while (height--) {
		uint32_t uvRow = uv;
		int const l = left >> 16;
		int const r = right >> 16;
		int const w = r-l;
		for (int i = 0; i < w; ++i) {
			uint16_t const texelOffset = (uvRow & 0xff00) | (uvRow >> 24);
			//rowPtr[l+i] = cpu->memory.load8(cpu, texPtr+texelOffset, NULL);
			rowPtr[l+i] = rawTexPtr[texelOffset];
			uvRow += uvPixelDelta;
		}
		left += leftDelta;
		right += rightDelta;
		uv += uvRowDelta;
		rowPtr += stride;
	}
}

static int32_t ClipColoredEdgePolygon(
	struct RenderParams const* params,
	uint8_t clipFlags,
	int32_t* vertBuffer)
{
	int32_t scratchBuffer[64];
	int32_t vertCount = 3;

	int32_t* r11 = vertBuffer;
	int32_t* r12 = scratchBuffer;

	int32_t r4,r8,r9;

	if ((clipFlags & kClipFlagTop) != 0)
	{
		int32_t const r5 = params->clipTop;	// ldrh r5, [sp, #42]
		int32_t const r6 = r5 << 16;		// mov r6, r5, lsl #16

		int32_t* r1 = r11;		// mov r1, r11
		int32_t* r2 = r12;		// mov r2, r12
		vertCount--;			// sub r10, r10, #1
		int32_t r3 = *r1++;		// ldr r3, [r1], #4

		do {
			r8 = r3 - r6;		// subs r8, r3, r6
			if (r8 >= 0)
			{
				*r2++ = r3;		// strge r3, [r2], #4
			}
			r4 = *r1++;			// ldr r4, [r1], #4
			r9 = r4 - r6;		// sub r9, r4, r6
			r8 ^= r9;			// eors r8, r8, r9
			if (r8 < 0)			// bpl
			{
				r8 = r4 >> 16;							// mov r8, r4, asr #16
				r8 -= r3 >> 16;							// sub r8, r8, r3, asr #16 
				r8 = DivTable(r8);						// ldr r8, [r14, r8, lsl #2]
				r9 = r5 - (r3 >> 16);					// sub r9, r5, r3, asr #16
				r9 = r8 * r9;							// mul r9, r8, r9
				r8 = r3 << 16;							// mov r8, r3, lsl #16
				r8 = (r4 << 16) - r8;					// rsb r8, r8, r4, lsl #16
				r8 = ((int64_t)r8 * (int64_t)r9) >> 32;	// smull r9, r8, r8, r9
				r8 *= 4;								// mov r8, r8, lsl #2
				r8 += r3 << 16;							// add r8, r8, r3, lsl #16
				r8 = r6 | ((uint32_t)r8 >> 16);			// orr r8, r6, r8, lsr #16
				*r2++ = r8;								// str r8, [r2], #4
			}
			r3 = r4;				// mov r3, r4
			vertCount--;			// subs r10, r10, #1
		} while(vertCount != 0);	// bne

		r8 = r3 - r6;	// subs r8, r3, r6
		if (r8 >= 0)
		{
			*r2++ = r3;	// strge r3, [r2], #4
		}
		r4 = *r11;		// ldr r4, [r11]
		r9 = r4 - r6;	// sub r9, r4, r6
		r8 ^= r9;		// eors r8, r8, r9
		if (r8 < 0)		// bpl
		{
			r8 = r4 >> 16;							// mov r8, r4, asr #16
			r8 -= r3 >> 16;							// sub r8, r8, r3, asr #16 
			r8 = DivTable(r8);						// ldr r8, [r14, r8, lsl #2]
			r9 = r5 - (r3 >> 16);					// sub r9, r5, r3, asr #16
			r9 = r8 * r9;							// mul r9, r8, r9
			r8 = r3 << 16;							// mov r8, r3, lsl #16
			r8 = (r4 << 16) - r8;					// rsb r8, r8, r4, lsl #16
			r8 = ((int64_t)r8 * (int64_t)r9) >> 32;	// smull r9, r8, r8, r9
			r8 *= 4;								// mov r8, r8, lsl #2
			r8 += r3 << 16;							// add r8, r8, r3, lsl #16
			r8 = r6 | ((uint32_t)r8 >> 16);			// orr r8, r6, r8, lsr #16
			*r2++ = r8;								// str r8, [r2], #4
		}

		vertCount = r2 - r12;	// sub r10, r2, r12 ; mov r10, r10, asr #2

		if (vertCount < 3)
			return vertCount;

		memcpy(vertBuffer, scratchBuffer, vertCount * sizeof(int32_t));
	}

	if ((clipFlags & kClipFlagLeft) != 0)
	{
		int32_t const r5 = params->clipLeft;	// ldrh r5, [sp, #38]
		int32_t const r6 = r5 << 16;			// mov r6, r5, lsl #16

		int32_t* r1 = r11;	// mov r1, r11
		int32_t* r2 = r12;	// mov r2, r12
		vertCount--;		// sub r10, r10, #1
		int32_t r3 = *r1++;	// ldr r3, [r1], #4

		do {
			r8 = (r3 << 16) - r6;	// rsbs r8, r6, r3, lsl #16
			if (r8 >= 0)
			{
				*r2++ = r3;		// strge r3, [r2], #4
			}
			r4 = *r1++;			// ldr r4, [r1], #4
			r9 = (r4<<16) - r6;	// rsb r9, r6, r4, lsl #16
			r8 ^= r9;			// eors r8, r8, r9
			if (r8 < 0)			// bpl
			{
				r9 = r3 << 16;							// mov r9, r3, lsl #16
				r8 = (r4 << 16) - r9;					// rsb r8, r9, r4, lsl #16 
				r8 = r8 >> 16;							// mov r8, r8, asr #16
				r8 = DivTable(r8);						// ldr r8, [r14, r8, lsl #2]
				r9 = r5 - (r9 >> 16);					// sub r9, r5, r9, asr #16
				r9 = r8 * r9;							// mul r9, r8, r9
				r8 = r3 >> 16;							// mov r8, r3, asr #16
				r8 = (r4 >> 16) - r8;					// rsb r8, r8, r4, asr #16
				r8 *= 4;								// mov r8, r8, lsl #2
				r8 = ((int64_t)r8 * (int64_t)r9) >> 32;	// smull r9, r8, r8, r9
				r8 += r3 >> 16;							// add r8, r8, r3, asr #16
				r8 = r5 | (r8 << 16);					// orr r8, r5, r8, lsl #16
				*r2++ = r8;								// str r8, [r2], #4
			}
			r3 = r4;		// mov r3, r4
			vertCount--;	// subs r10, r10, #1
		} while (vertCount != 0);	// bne

		r8 = (r3 << 16) - r6;	// rsbs r8, r6, r3, lsl #16
		if (r8 >= 0)
		{
			*r2++ = r3;			// strge r3, [r2], #4
		}
		r4 = *r11;				// ldr r4, [r11]
		r9 = (r4 << 16) - r6;	// rsb r9, r6, r4, lsl #16
		r8 ^= r9;				// eors r8, r8, r9
		if (r8 < 0)				// bpl
		{
			r9 = r3 << 16;							// mov r9, r3, lsl #16
			r8 = (r4 << 16) - r9;					// rsb r8, r9, r4, lsl #16 
			r8 = r8 >> 16;							// mov r8, r8, asr #16
			r8 = DivTable(r8);						// ldr r8, [r14, r8, lsl #2]
			r9 = r5 - (r9 >> 16);					// sub r9, r5, r9, asr #16
			r9 = r8 * r9;							// mul r9, r8, r9
			r8 = r3 >> 16;							// mov r8, r3, asr #16
			r8 = (r4 >> 16) - r8;					// rsb r8, r8, r4, asr #16
			r8 *= 4;								// mov r8, r8, lsl #2
			r8 = ((int64_t)r8 * (int64_t)r9) >> 32;	// smull r9, r8, r8, r9
			r8 += r3 >> 16;							// add r8, r8, r3, asr #16
			r8 = r5 | (r8 << 16);					// orr r8, r5, r8, lsl #16
			*r2++ = r8;								// str r8, [r2], #4
		}

		vertCount = r2 - r12;	// sub r10, r2, r12 ; mov r10, r10, asr #2

		if (vertCount < 3)
			return vertCount;

		memcpy(vertBuffer, scratchBuffer, vertCount * sizeof(int32_t));
	}

	if ((clipFlags & kClipFlagRight) != 0)
	{
		int32_t const r5 = params->clipRight;	// ldrh r5, [sp, #36]
		int32_t const r6 = r5 << 16;			// mov r6, r5, lsl #16

		int32_t* r1 = r11;	// mov r1, r11
		int32_t* r2 = r12;	// mov r2, r12
		vertCount--;		// sub r10, r10, #1
		int32_t r3 = *r1++;	// ldr r3, [r1], #4

		do {
			r8 = (r3 << 16) - r6;	// rsbs r8, r6, r3, lsl #16
			if (r8 <= 0)
			{
				*r2++ = r3;			// strle r3, [r2], #4
			}
			r4 = *r1++;				// ldr r4, [r1], #4
			r9 = (r4 << 16) - r6;	// rsb r9, r6, r4, lsl #16
			r8 ^= r9;				// eors r8, r8, r9
			if (r8 < 0)	// bpl
			{
				r9 = r3 << 16;							// mov r9, r3, lsl #16
				r8 = (r4 << 16) - r9;					// rsb r8, r9, r4, lsl #16 
				r8 = r8 >> 16;							// mov r8, r8, asr #16
				r8 = DivTable(r8);						// ldr r8, [r14, r8, lsl #2]
				r9 = r5 - (r9 >> 16);					// sub r9, r5, r9, asr #16
				r9 = r8 * r9;							// mul r9, r8, r9
				r8 = r3 >> 16;							// mov r8, r3, asr #16
				r8 = (r4 >> 16) - r8;					// rsb r8, r8, r4, asr #16
				r8 *= 4;								// mov r8, r8, lsl #2
				r8 = ((int64_t)r8 * (int64_t)r9) >> 32;	// smull r9, r8, r8, r9
				r8 += r3 >> 16;							// add r8, r8, r3, asr #16
				r8 = r5 | (r8 << 16);					// orr r8, r5, r8, lsl #16
				*r2++ = r8;								// str r8, [r2], #4
			}
			r3 = r4;		// mov r3, r4
			vertCount--;	// subs r10, r10, #1
		} while (vertCount != 0);	// bne

		r8 = (r3 << 16) - r6;	// rsbs r8, r6, r3, lsl #16
		if (r8 <= 0)
		{
			*r2++ = r3;			// strle r3, [r2], #4
		}
		r4 = *r11;				// ldr r4, [r11]
		r9 = (r4 << 16) - r6;	// rsb r9, r6, r4, lsl #16
		r8 ^= r9;				// eors r8, r8, r9
		if (r8 < 0)				// bpl
		{
			r9 = r3 << 16;							// mov r9, r3, lsl #16
			r8 = (r4 << 16) - r9;					// rsb r8, r9, r4, lsl #16 
			r8 = r8 >> 16;							// mov r8, r8, asr #16
			r8 = DivTable(r8);						// ldr r8, [r14, r8, lsl #2]
			r9 = r5 - (r9 >> 16);					// sub r9, r5, r9, asr #16
			r9 = r8 * r9;							// mul r9, r8, r9
			r8 = r3 >> 16;							// mov r8, r3, asr #16
			r8 = (r4 >> 16) - r8;					// rsb r8, r8, r4, asr #16
			r8 *= 4;								// mov r8, r8, lsl #2
			r8 = ((int64_t)r8 * (int64_t)r9) >> 32;	// smull r9, r8, r8, r9
			r8 += r3 >> 16;							// add r8, r8, r3, asr #16
			r8 = r5 | (r8 << 16);					// orr r8, r5, r8, lsl #16
			*r2++ = r8;								// str r8, [r2], #4
		}

		vertCount = r2 - r12;	// sub r10, r2, r12 ; mov r10, r10, asr #2

		if (vertCount < 3)
			return vertCount;

		memcpy(vertBuffer, scratchBuffer, vertCount * sizeof(int32_t));
	}

	if ((clipFlags & kClipFlagBottom) != 0)
	{
		int32_t const r5 = params->clipBottom;	// ldrh r5, [sp, #40]
		int32_t const r6 = r5 << 16;			// mov r6, r5, lsl #16

		int32_t* r1 = r11;	// mov r1, r11
		int32_t* r2 = r12;	// mov r2, r12
		vertCount--;		// sub r10, r10, #1
		int32_t r3 = *r1++;	// ldr r3, [r1], #4

		do {
			r8 = r3 - r6;	// subs r8, r3, r6
			if (r8 <= 0)
			{
				*r2++ = r3;	// strle r3, [r2], #4
			}
			r4 = *r1++;		// ldr r4, [r1], #4
			r9 = r4 - r6;	// sub r9, r4, r6
			r8 ^= r9;		// eors r8, r8, r9
			if (r8 < 0)		// bpl
			{
				r8 = r4 >> 16;							// mov r8, r4, asr #16
				r8 -= r3 >> 16;							// sub r8, r8, r3, asr #16 
				r8 = DivTable(r8);						// ldr r8, [r14, r8, lsl #2]
				r9 = r5 - (r3 >> 16);					// sub r9, r5, r3, asr #16
				r9 = r8 * r9;							// mul r9, r8, r9
				r8 = r3 << 16;							// mov r8, r3, lsl #16
				r8 = (r4 << 16) - r8;					// rsb r8, r8, r4, lsl #16
				r8 = ((int64_t)r8 * (int64_t)r9) >> 32;	// smull r9, r8, r8, r9
				r8 *= 4;								// mov r8, r8, lsl #2
				r8 += r3 << 16;							// add r8, r8, r3, lsl #16
				r8 = r6 | ((uint32_t)r8 >> 16);			// orr r8, r6, r8, lsr #16
				*r2++ = r8;								// str r8, [r2], #4
			}
			r3 = r4;		// mov r3, r4
			vertCount--;	// subs r10, r10, #1
		} while (vertCount != 0); // bne

		r8 = r3 - r6;	// subs r8, r3, r6
		if (r8 <= 0)
		{
			*r2++ = r3;	// strle r3, [r2], #4
		}
		r4 = *r11;		// ldr r4, [r11]
		r9 = r4 - r6;	// sub r9, r4, r6
		r8 ^= r9;		// eors r8, r8, r9
		if (r8 < 0)		// bpl
		{
			r8 = r4 >> 16;							// mov r8, r4, asr #16
			r8 -= r3 >> 16;							// sub r8, r8, r3, asr #16 
			r8 = DivTable(r8);						// ldr r8, [r14, r8, lsl #2]
			r9 = r5 - (r3 >> 16);					// sub r9, r5, r3, asr #16
			r9 = r8 * r9;							// mul r9, r8, r9
			r8 = r3 << 16;							// mov r8, r3, lsl #16
			r8 = (r4 << 16) - r8;					// rsb r8, r8, r4, lsl #16
			r8 = ((int64_t)r8 * (int64_t)r9) >> 32;	// smull r9, r8, r8, r9
			r8 *= 4;								// mov r8, r8, lsl #2
			r8 += r3 << 16;							// add r8, r8, r3, lsl #16
			r8 = r6 | ((uint32_t)r8 >> 16);			// orr r8, r6, r8, lsr #16
			*r2++ = r8;								// str r8, [r2], #4
		}

		vertCount = r2 - r12;	// sub r10, r2, r12 ; mov r10, r10, asr #2

		if (vertCount < 3)
			return vertCount;

		memcpy(vertBuffer, scratchBuffer, vertCount * sizeof(int32_t));
	}

	return vertCount;
}

static int32_t ClipAffineTexEdgePolygon(
	struct RenderParams const* params,
	uint8_t clipFlags,
	int32_t* vertBuffer)
{
	int32_t scratchBuffer[64];
	int32_t vertCount = 3;

	struct MultiPtr r11;
	struct MultiPtr r12;
	r11.i32 = vertBuffer;
	r12.i32 = scratchBuffer;

	int32_t r0,r4,r8,r9;

	if ((clipFlags & kClipFlagTop) != 0)
	{
		int32_t const r5 = params->clipTop;	// ldrh r5, [sp, #42]
		int32_t const r6 = r5 << 16;		// mov r6, r5, lsl #16

		struct MultiPtr r1 = r11;			// mov r1, r11
		struct MultiPtr r2 = r12;			// mov r2, r12
		vertCount--;						// sub r10, r10, #1
		int32_t r3 = *r1.i32; r1.u8 += 8;	// ldr r3, [r1], #8

		do {
			r8 = r3 - r6;	// subs r8, r3, r6
			if (r8 >= 0)
			{
				r4 = r1.i32[-1];	// ldrge r4, [r1, #-4]
				r2.i32[0] = r3;
				r2.i32[1] = r4;
				r2.u8 += 8;			// stmgeia r2!, { r3,r4 } 
			}
			r4 = r1.i32[0]; r1.u8 += 8;	// ldr r4, [r1], #8
			r9 = r4 - r6;				// sub r9, r4, r6
			r8 ^= r9;					// eors r8, r8, r9
			if (r8 < 0) // bpl
			{
				r8 = r4 >> 16;							// mov r8, r4, asr #16
				r8 -= r3 >> 16;							// sub r8, r8, r3, asr #16 
				r8 = DivTable(r8);						// ldr r8, [r14, r8, lsl #2]
				r0 = r5 - (r3 >> 16);					// sub r0, r5, r3, asr #16
				r0 = r8 * r0;							// mul r0, r8, r0
				r8 = r3 << 16;							// mov r8, r3, lsl #16
				r8 = (r4 << 16) - r8;					// rsb r8, r8, r4, lsl #16
				r8 = ((int64_t)r8 * (int64_t)r0) >> 32;	// smull r9, r8, r8, r0
				r8 *= 4;								// mov r8, r8, lsl #2
				r8 += r3 << 16;							// add r8, r8, r3, lsl #16
				r8 = r6 | ((uint32_t)r8 >> 16);			// orr r8, r6, r8, lsr #16
				r2.i32[0] = r8; r2.u8 += 4;				// str r8, [r2], #4

				r8 = r1.u16[-6];			// ldrh r8, [r1, #-12]
				r9 = r1.u16[-2];			// ldrh r9, [r1, #-4]
				r9 -= r8;					// sub r9, r9, r8
				r9 *= 4;					// mov r9, r9, lsl #2
				smlal(r9, r8, r9, r0);		// smlal r9, r8, r9, r0
				*r2.u16 = r8; r2.u8 += 2;	// strh r8, [r2], #2
				r8 = r1.u16[-5];			// ldrh r8, [r1, #-10]
				r9 = r1.u16[-1];			// ldrh r9, [r1, #-2]
				r9 -= r8;					// sub r9, r9, r8
				r9 *= 4;					// mov r9, r9, lsl #2
				smlal(r9, r8, r9, r0);		// smlal r9, r8, r9, r0
				*r2.u16 = r8; r2.u8 += 2;	// strh r8, [r2], #2
			}
			r3 = r4;		// mov r3, r4
			vertCount--;	// subs r10, r10, #1
		} while (vertCount != 0);	// bne

		r8 = r3 - r6;	// subs r8, r3, r6
		if (r8 >= 0)
		{
			r4 = r1.i32[-1];	// ldrge r4, [r1, #-4]
			r2.i32[0] = r3;
			r2.i32[1] = r4;
			r2.u8 += 8;			// stmgeia r2!, { r3,r4 }
		}
		r4 = *r11.i32;	// ldr r4, [r11]
		r9 = r4 - r6;	// sub r9, r4, r6
		r8 ^= r9;		// eors r8, r8, r9
		if (r8 < 0)		// bpl
		{
			r8 = r4 >> 16;							// mov r8, r4, asr #16
			r8 -= r3 >> 16;							// sub r8, r8, r3, asr #16 
			r8 = DivTable(r8);						// ldr r8, [r14, r8, lsl #2]
			r0 = r5 - (r3 >> 16);					// sub r0, r5, r3, asr #16
			r0 = r8 * r0;							// mul r0, r8, r0
			r8 = r3 << 16;							// mov r8, r3, lsl #16
			r8 = (r4 << 16) - r8;					// rsb r8, r8, r4, lsl #16
			r8 = ((int64_t)r8 * (int64_t)r0) >> 32;	// smull r9, r8, r8, r0
			r8 *= 4;								// mov r8, r8, lsl #2
			r8 += r3 << 16;							// add r8, r8, r3, lsl #16
			r8 = r6 | ((uint32_t)r8 >> 16);			// orr r8, r6, r8, lsr #16
			*r2.i32 = r8; r2.u8 += 4;				// str r8, [r2], #4

			r8 = r1.u16[-2];			// ldrh r8, [r1, #-4]
			r9 = r11.u16[2];			// ldrh r9, [r11, #4]
			r9 -= r8;					// sub r9, r9, r8
			r9 *= 4;					// mov r9, r9, lsl #2
			smlal(r9, r8, r9, r0);		// smlal r9, r8, r9, r0
			*r2.u16 = r8; r2.u8 += 2;	// strh r8, [r2], #2
			r8 = r1.u16[-1];			// ldrh r8, [r1, #-2]
			r9 = r11.u16[3];			// ldrh r9, [r11, #6]
			r9 -= r8;					// sub r9, r9, r8
			r9 *= 4;					// mov r9, r9, lsl #2
			smlal(r9, r8, r9, r0);		// smlal r9, r8, r9, r0
			*r2.u16 = r8; r2.u8 += 2;	// strh r8, [r2], #2
		}

		vertCount = (r2.u8 - r12.u8) / 8;

		if (vertCount < 3)
			return vertCount;

		memcpy(vertBuffer, scratchBuffer, vertCount * sizeof(int32_t) * 2);
	}

	if ((clipFlags & kClipFlagLeft) != 0)
	{
		int32_t const r5 = params->clipLeft;	// ldrh r5, [sp, #38]
		int32_t const r6 = r5 << 16;			// mov r6, r5, lsl #16

		struct MultiPtr r1 = r11;			// mov r1, r11
		struct MultiPtr r2 = r12;			// mov r2, r12
		vertCount--;						// sub r10, r10, #1
		int32_t r3 = *r1.i32; r1.u8 += 8;	// ldr r3, [r1], #8

		do {
			r8 = (r3 << 16) - r6;	// rsbs r8, r6, r3, lsl #16
			if (r8 >= 0)
			{
				r4 = r1.i32[-1];	// ldrge r4, [r1, #-4]
				r2.i32[0] = r3;
				r2.i32[1] = r4;
				r2.u8 += 8;			// stmgeia r2!, { r3,r4 }
			}
			r4 = *r1.i32; r1.u8 += 8;	// ldr r4, [r1], #8
			r9 = (r4 << 16) - r6;		// rsb r9, r6, r4, lsl #16
			r8 ^= r9;					// eors r8, r8, r9
			if (r8 < 0)	// bpl
			{
				r9 = r3 << 16;							// mov r9, r3, lsl #16
				r8 = (r4 << 16) - r9;					// rsb r8, r9, r4, lsl #16 
				r8 = r8 >> 16;							// mov r8, r8, asr #16
				r8 = DivTable(r8);						// ldr r8, [r14, r8, lsl #2]
				r0 = r5 - (r9 >> 16);					// sub r0, r5, r9, asr #16
				r0 = r8 * r0;							// mul r0, r8, r0
				r8 = r3 >> 16;							// mov r8, r3, asr #16
				r8 = (r4 >> 16) - r8;					// rsb r8, r8, r4, asr #16
				r8 *= 4;								// mov r8, r8, lsl #2
				r8 = ((int64_t)r8 * (int64_t)r0) >> 32;	// smull r9, r8, r8, r0
				r8 += r3 >> 16;							// add r8, r8, r3, asr #16
				r8 = r5 | (r8 << 16);					// orr r8, r5, r8, lsl #16
				*r2.i32 = r8; r2.u8 += 4;				// str r8, [r2], #4

				r8 = r1.u16[-6];			// ldrh r8, [r1, #-12]
				r9 = r1.u16[-2];			// ldrh r9, [r1, #-4]
				r9 -= r8;					// sub r9, r9, r8
				r9 *= 4;					// mov r9, r9, lsl #2
				smlal(r9, r8, r9, r0);		// smlal r9, r8, r9, r0
				*r2.u16 = r8; r2.u8 += 2;	// strh r8, [r2], #2
				r8 = r1.u16[-5];			// ldrh r8, [r1, #-10]
				r9 = r1.u16[-1];			// ldrh r9, [r1, #-2]
				r9 -= r8;					// sub r9, r9, r8
				r9 *= 4;					// mov r9, r9, lsl #2
				smlal(r9, r8, r9, r0);		// smlal r9, r8, r9, r0
				*r2.u16 = r8; r2.u8 += 2;	// strh r8, [r2], #2
			}
			r3 = r4;		// mov r3, r4
			vertCount--;	// subs r10, r10, #1
		} while (vertCount != 0);	// bne

		r8 = (r3 << 16) - r6;	// rsbs r8, r6, r3, lsl #16
		if (r8 >= 0)
		{
			r4 = r1.i32[-1];	// ldrge r4, [r1, #-4]
			r2.i32[0] = r3;
			r2.i32[1] = r4;
			r2.u8 += 8;			// stmgeia r2!, { r3,r4 }
		}
		r4 = *r11.i32;			// ldr r4, [r11]
		r9 = (r4 << 16) - r6;	// rsb r9, r6, r4, lsl #16
		r8 ^= r9;				// eors r8, r8, r9
		if (r8 < 0)	// bpl
		{
			r9 = r3 << 16;							// mov r9, r3, lsl #16
			r8 = (r4 << 16) - r9;					// rsb r8, r9, r4, lsl #16 
			r8 = r8 >> 16;							// mov r8, r8, asr #16
			r8 = DivTable(r8);						// ldr r8, [r14, r8, lsl #2]
			r0 = r5 - (r9 >> 16);					// sub r0, r5, r9, asr #16
			r0 = r8 * r0;							// mul r0, r8, r0
			r8 = r3 >> 16;							// mov r8, r3, asr #16
			r8 = (r4 >> 16) - r8;					// rsb r8, r8, r4, asr #16
			r8 *= 4;								// mov r8, r8, lsl #2
			r8 = ((int64_t)r8 * (int64_t)r0) >> 32;	// smull r9, r8, r8, r0
			r8 += r3 >> 16;							// add r8, r8, r3, asr #16
			r8 = r5 | (r8 << 16);					// orr r8, r5, r8, lsl #16
			*r2.i32 = r8; r2.u8 += 4;				// str r8, [r2], #4

			r8 = r1.u16[-2];			// ldrh r8, [r1, #-4]
			r9 = r11.u16[2];			// ldrh r9, [r11, #4]
			r9 -= r8;					// sub r9, r9, r8
			r9 *= 4;					// mov r9, r9, lsl #2
			smlal(r9, r8, r9, r0);		// smlal r9, r8, r9, r0
			*r2.u16 = r8; r2.u8 += 2;	// strh r8, [r2], #2
			r8 = r1.u16[-1];			// ldrh r8, [r1, #-2]
			r9 = r11.u16[3];			// ldrh r9, [r11, #6]
			r9 -= r8;					// sub r9, r9, r8
			r9 *= 4;					// mov r9, r9, lsl #2
			smlal(r9, r8, r9, r0);		// smlal r9, r8, r9, r0
			*r2.u16 = r8; r2.u8 += 2;	// strh r8, [r2], #2
		}

		vertCount = (r2.u8 - r12.u8) / 8;

		if (vertCount < 3)
			return vertCount;

		memcpy(vertBuffer, scratchBuffer, vertCount * sizeof(int32_t) * 2);
	}

	if ((clipFlags & kClipFlagRight) != 0)
	{
		int32_t const r5 = params->clipRight;	// ldrh r5, [sp, #36]
		int32_t const r6 = r5 << 16;			// mov r6, r5, lsl #16

		struct MultiPtr r1 = r11;			// mov r1, r11
		struct MultiPtr r2 = r12;			// mov r2, r12
		vertCount--;						// sub r10, r10, #1
		int32_t r3 = *r1.i32; r1.u8 += 8;	// ldr r3, [r1], #8

		do {
			r8 = (r3 << 16) - r6;	// rsbs r8, r6, r3, lsl #16
			if (r8 <= 0)
			{
				r4 = r1.i32[-1];	// ldrle r4, [r1, #-4]
				r2.i32[0] = r3;
				r2.i32[1] = r4;
				r2.u8 += 8;			// stmleia r2!, { r3,r4 }
			}
			r4 = *r1.i32; r1.u8 += 8;	// ldr r4, [r1], #8
			r9 = (r4 << 16) - r6;		// rsb r9, r6, r4, lsl #16
			r8 ^= r9;					// eors r8, r8, r9
			if (r8 < 0)	// bpl
			{
				r9 = r3 << 16;							// mov r9, r3, lsl #16
				r8 = (r4 << 16) - r9;					// rsb r8, r9, r4, lsl #16 
				r8 = r8 >> 16;							// mov r8, r8, asr #16
				r8 = DivTable(r8);						// ldr r8, [r14, r8, lsl #2]
				r0 = r5 - (r9 >> 16);					// sub r0, r5, r9, asr #16
				r0 = r8 * r0;							// mul r0, r8, r0
				r8 = r3 >> 16;							// mov r8, r3, asr #16
				r8 = (r4 >> 16) - r8;					// rsb r8, r8, r4, asr #16
				r8 *= 4;								// mov r8, r8, lsl #2
				r8 = ((int64_t)r8 * (int64_t)r0) >> 32;	// smull r9, r8, r8, r0
				r8 += r3 >> 16;							// add r8, r8, r3, asr #16
				r8 = r5 | (r8 << 16);					// orr r8, r5, r8, lsl #16
				*r2.i32 = r8; r2.u8 += 4;				// str r8, [r2], #4

				r8 = r1.u16[-6];			// ldrh r8, [r1, #-12]
				r9 = r1.u16[-2];			// ldrh r9, [r1, #-4]
				r9 -= r8;					// sub r9, r9, r8
				r9 *= 4;					// mov r9, r9, lsl #2
				smlal(r9, r8, r9, r0);		// smlal r9, r8, r9, r0
				*r2.u16 = r8; r2.u8 += 2;	// strh r8, [r2], #2
				r8 = r1.u16[-5];			// ldrh r8, [r1, #-10]
				r9 = r1.u16[-1];			// ldrh r9, [r1, #-2]
				r9 -= r8;					// sub r9, r9, r8
				r9 *= 4;					// mov r9, r9, lsl #2
				smlal(r9, r8, r9, r0);		// smlal r9, r8, r9, r0
				*r2.u16 = r8; r2.u8 += 2;	// strh r8, [r2], #2
			}
			r3 = r4;		// mov r3, r4
			vertCount--;	// subs r10, r10, #1
		} while (vertCount != 0);	// bne

		r8 = (r3 << 16) - r6;	// rsbs r8, r6, r3, lsl #16
		if (r8 <= 0)
		{
			r4 = r1.i32[-1];	// ldrle r4, [r1, #-4]
			r2.i32[0] = r3;
			r2.i32[1] = r4;
			r2.u8 += 8;			// stmleia r2!, { r3,r4 }
		}
		r4 = *r11.i32;			// ldr r4, [r11] 
		r9 = (r4 << 16) - r6;	// rsb r9, r6, r4, lsl #16
		r8 ^= r9;				// eors r8, r8, r9
		if (r8 < 0)	// bpl
		{
			r9 = r3 << 16;							// mov r9, r3, lsl #16
			r8 = (r4 << 16) - r9;					// rsb r8, r9, r4, lsl #16 
			r8 = r8 >> 16;							// mov r8, r8, asr #16
			r8 = DivTable(r8);						// ldr r8, [r14, r8, lsl #2]
			r0 = r5 - (r9 >> 16);					// sub r0, r5, r9, asr #16
			r0 = r8 * r0;							// mul r0, r8, r0
			r8 = r3 >> 16;							// mov r8, r3, asr #16
			r8 = (r4 >> 16) - r8;					// rsb r8, r8, r4, asr #16
			r8 *= 4;								// mov r8, r8, lsl #2
			r8 = ((int64_t)r8 * (int64_t)r0) >> 32;	// smull r9, r8, r8, r0
			r8 += r3 >> 16;							// add r8, r8, r3, asr #16
			r8 = r5 | (r8 << 16);					// orr r8, r5, r8, lsl #16
			*r2.i32 = r8; r2.u8 += 4;				// str r8, [r2], #4

			r8 = r1.u16[-2];			// ldrh r8, [r1, #-4]
			r9 = r11.u16[2];			// ldrh r9, [r11, #4]
			r9 -= r8;					// sub r9, r9, r8
			r9 *= 4;					// mov r9, r9, lsl #2
			smlal(r9, r8, r9, r0);		// smlal r9, r8, r9, r0
			*r2.u16 = r8; r2.u8 += 2;	// strh r8, [r2], #2
			r8 = r1.u16[-1];			// ldrh r8, [r1, #-2]
			r9 = r11.u16[3];			// ldrh r9, [r11, #6]
			r9 -= r8;					// sub r9, r9, r8
			r9 *= 4;					// mov r9, r9, lsl #2
			smlal(r9, r8, r9, r0);		// smlal r9, r8, r9, r0
			*r2.u16 = r8; r2.u8 += 2;	// strh r8, [r2], #2
		}

		vertCount = (r2.u8 - r12.u8) / 8;

		if (vertCount < 3)
			return vertCount;

		memcpy(vertBuffer, scratchBuffer, vertCount * sizeof(int32_t) * 2);
	}

	if ((clipFlags & kClipFlagBottom) != 0)
	{
		int32_t const r5 = params->clipBottom;	// ldrh r5, [sp, #40]
		int32_t const r6 = r5 << 16;			// mov r6, r5, lsl #16

		struct MultiPtr r1 = r11;			// mov r1, r11
		struct MultiPtr r2 = r12;			// mov r2, r12
		vertCount--;						// sub r10, r10, #1
		int32_t r3 = *r1.i32; r1.u8 += 8;	// ldr r3, [r1], #8

		do {
			r8 = r3 - r6;	// subs r8, r3, r6
			if (r8 <= 0)
			{
				r4 = r1.i32[-1];	// ldrle r4, [r1, #-4]
				r2.i32[0] = r3;
				r2.i32[1] = r4;
				r2.u8 += 8;			// stmleia r2!, { r3,r4 }
			}
			r4 = *r1.i32; r1.u8 += 8;	// ldr r4, [r1], #8
			r9 = r4 - r6;				// sub r9, r4, r6
			r8 ^= r9;					// eors r8, r8, r9
			if (r8 < 0)	// bpl
			{
				r8 = r4 >> 16;							// mov r8, r4, asr #16
				r8 -= r3 >> 16;							// sub r8, r8, r3, asr #16 
				r8 = DivTable(r8);						// ldr r8, [r14, r8, lsl #2]
				r0 = r5 - (r3 >> 16);					// sub r0, r5, r3, asr #16
				r0 = r8 * r0;							// mul r0, r8, r0
				r8 = r3 << 16;							// mov r8, r3, lsl #16
				r8 = (r4 << 16) - r8;					// rsb r8, r8, r4, lsl #16
				r8 = ((int64_t)r8 * (int64_t)r0) >> 32;	// smull r9, r8, r8, r0
				r8 *= 4;								// mov r8, r8, lsl #2
				r8 += (r3 << 16);						// add r8, r8, r3, lsl #16
				r8 = r6 | ((uint32_t)r8 >> 16);			// orr r8, r6, r8, lsr #16
				*r2.i32 = r8; r2.u8 += 4;				// str r8, [r2], #4

				r8 = r1.u16[-6];			// ldrh r8, [r1, #-12]
				r9 = r1.u16[-2];			// ldrh r9, [r1, #-4]
				r9 -= r8;					// sub r9, r9, r8
				r9 *= 4;					// mov r9, r9, lsl #2
				smlal(r9, r8, r9, r0);		// smlal r9, r8, r9, r0
				*r2.u16 = r8; r2.u8 += 2;	// strh r8, [r2], #2
				r8 = r1.u16[-5];			// ldrh r8, [r1, #-10]
				r9 = r1.u16[-1];			// ldrh r9, [r1, #-2]
				r9 -= r8;					// sub r9, r9, r8
				r9 *= 4;					// mov r9, r9, lsl #2
				smlal(r9, r8, r9, r0);		// smlal r9, r8, r9, r0
				*r2.u16 = r8; r2.u8 += 2;	// strh r8, [r2], #2
			}
			
			r3 = r4;		// mov r3, r4
			vertCount--;	// subs r10, r10, #1
		} while (vertCount != 0);	// bne

		r8 = r3 - r6;	// subs r8, r3, r6
		if (r8 <= 0)
		{
			r4 = r1.i32[-1];	// ldrle r4, [r1, #-4]
			r2.i32[0] = r3;
			r2.i32[1] = r4;
			r2.u8 += 8;			// stmleia r2!, { r3,r4 }
		}
		r4 = *r11.i32;	// ldr r4, [r11]
		r9 = r4 - r6;	// sub r9, r4, r6
		r8 ^= r9;		// eors r8, r8, r9
		if (r8 < 0)	// bpl
		{
			r8 = r4 >> 16;							// mov r8, r4, asr #16
			r8 -= r3 >> 16;							// sub r8, r8, r3, asr #16 
			r8 = DivTable(r8);						// ldr r8, [r14, r8, lsl #2]
			r0 = r5 - (r3 >> 16);					// sub r0, r5, r3, asr #16
			r0 = r8 * r0;							// mul r0, r8, r0
			r8 = r3 << 16;							// mov r8, r3, lsl #16
			r8 = (r4 << 16) - r8;					// rsb r8, r8, r4, lsl #16
			r8 = ((int64_t)r8 * (int64_t)r0) >> 32;	// smull r9, r8, r8, r0
			r8 *= 4;								// mov r8, r8, lsl #2
			r8 += r3 << 16;							// add r8, r8, r3, lsl #16
			r8 = r6 | ((uint32_t)r8 >> 16);			// orr r8, r6, r8, lsr #16
			*r2.i32 = r8; r2.u8 += 4;				// str r8, [r2], #4

			r8 = r1.u16[-2];			// ldrh r8, [r1, #-4]
			r9 = r11.u16[2];			// ldrh r9, [r11, #4]
			r9 -= r8;					// sub r9, r9, r8
			r9 *= 4;					// mov r9, r9, lsl #2
			smlal(r9, r8, r9, r0);		// smlal r9, r8, r9, r0
			*r2.u16 = r8; r2.u8 += 2;	// strh r8, [r2], #2
			r8 = r1.u16[-1];			// ldrh r8, [r1, #-2]
			r9 = r11.u16[3];			// ldrh r9, [r11, #6]
			r9 -= r8;					// sub r9, r9, r8
			r9 *= 4;					// mov r9, r9, lsl #2
			smlal(r9, r8, r9, r0);		// smlal r9, r8, r9, r0
			*r2.u16 = r8; r2.u8 += 2;	// strh r8, [r2], #2
		}

		vertCount = (r2.u8 - r12.u8) / 8;

		if (vertCount < 3)
			return vertCount;

		memcpy(vertBuffer, scratchBuffer, vertCount * sizeof(int32_t) * 2);
	}

	return vertCount;
}

static void FillColoredTri(
	uint8_t* renderTarget,
	int scale,
	int32_t yx0, int32_t yx1, int32_t yx2,
	uint8_t colorIndex)
{
	if (yx0 > yx1) { XorSwap(&yx0, &yx1); }
	if (yx0 > yx2) { XorSwap(&yx0, &yx2); }
	if (yx1 > yx2) { XorSwap(&yx2, &yx1); }

	int16_t const Ay = (int16_t)(yx0 >> 16) * scale;
	int16_t const By = (int16_t)(yx1 >> 16) * scale;
	int16_t const Cy = (int16_t)(yx2 >> 16) * scale;

	int16_t const Ax = (int16_t)(yx0 & 0xffff) * scale;
	int16_t const Bx = (int16_t)(yx1 & 0xffff) * scale;
	int16_t const Cx = (int16_t)(yx2 & 0xffff) * scale;

	int32_t const ABdx = ((int64_t)((Bx - Ax) << 18) * (int64_t)DivTable(By - Ay)) >> 32;
	int32_t const BCdx = ((int64_t)((Cx - Bx) << 18) * (int64_t)DivTable(Cy - By)) >> 32;
	int32_t const ACdx = ((int64_t)((Cx - Ax) << 18) * (int64_t)DivTable(Cy - Ay)) >> 32;

	int32_t const subpixelA = 8 - (Ay & 7);
	int32_t const subpixelB = 8 - (By & 7);

	int32_t const ABx = (Ax << 13) + ((ABdx * subpixelA) / 8);
	int32_t const BCx = (Bx << 13) + ((BCdx * subpixelB) / 8);
	int32_t const ACx = (Ax << 13) + ((ACdx * subpixelA) / 8);

	int32_t const heightAB = (By/8) - (Ay/8);
	int32_t const heightBC = (Cy/8) - (By/8);

	if (ABdx > ACdx)
	{
		if (heightAB != 0) {
			FillColoredTrapezoid(
				renderTarget, scale,
				ACx, ACdx,
				ABx, ABdx,
				Ay/8, heightAB,
				colorIndex);
		}

		if (heightBC != 0) {
			FillColoredTrapezoid(
				renderTarget, scale,
				ACx + heightAB*ACdx, ACdx,
				BCx, BCdx,
				By/8, heightBC,
				colorIndex);
		}
	}
	else
	{
		if (heightAB != 0) {
			FillColoredTrapezoid(
				renderTarget, scale,
				ABx, ABdx,
				ACx, ACdx,
				Ay/8, heightAB,
				colorIndex);
		}

		if (heightBC != 0) {
			FillColoredTrapezoid(
				renderTarget, scale,
				BCx, BCdx,
				ACx + heightAB*ACdx, ACdx,
				By/8, heightBC,
				colorIndex);
		}
	}
}

static void FillAffineTexTri(
	uint8_t* renderTarget,
	int scale,
	int32_t yx0, int32_t yx1, int32_t yx2,
	uint32_t vu0, uint32_t vu1, uint32_t vu2,
	uint32_t texPtr,
	struct ARMCore* cpu)
{
	if (yx0 > yx1) { XorSwap(&yx0, &yx1); XorSwap(&vu0, &vu1); }
	if (yx0 > yx2) { XorSwap(&yx0, &yx2); XorSwap(&vu0, &vu2); }
	if (yx1 > yx2) { XorSwap(&yx2, &yx1); XorSwap(&vu2, &vu1); }

	int16_t const Ay = (int16_t)(yx0 >> 16) * scale;
	int16_t const By = (int16_t)(yx1 >> 16) * scale;
	int16_t const Cy = (int16_t)(yx2 >> 16) * scale;
	int16_t const Ax = (int16_t)(yx0 & 0xffff) * scale;
	int16_t const Bx = (int16_t)(yx1 & 0xffff) * scale;
	int16_t const Cx = (int16_t)(yx2 & 0xffff) * scale;

	uint16_t const Av = (uint16_t)(vu0 >> 16);
	uint16_t const Bv = (uint16_t)(vu1 >> 16);
	uint16_t const Cv = (uint16_t)(vu2 >> 16);
	uint16_t const Au = (uint16_t)(vu0 & 0xffff);
	uint16_t const Bu = (uint16_t)(vu1 & 0xffff);
	uint16_t const Cu = (uint16_t)(vu2 & 0xffff);

	uint8_t subpixelOffsetBC;
	int32_t invHeightBC, BCdx, BCx;
	int32_t invHeightAB, ABdx, ABx;
	int32_t              ACdx, ACx;
	int32_t r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r14;

	r7 = Cy - By;								// sub r7, r6, r5
	r7 = DivTable(r7);							// ldr r7, [lr, r7, lsl #2]
	invHeightBC = r7;							// str r7, [sp, #112]
	r8 = Cx - Bx;								// sub r8, r3, r2
	r8 = r8 << 18;								// mov r8, r8, lsl #18
	r7 = ((int64_t)r8 * (int64_t)r7) >> 32;		// smull r8, r7, r8, r7
	BCdx = r7;									// str r7, [sp, #8]
	r8 = By & 7;								// and r8, r5, #7
	r8 = 8 - r8;								// rsb r8, r8, #8
	subpixelOffsetBC = r8;						// strb r8, [sp, #116]
	r8 = r7 * r8;								// mul r8, r7, r8
	r8 /= 8;									// mov r8, r8, asr #3
	r8 += Bx << 13;								// add r8, r8, r2, lsl #13
	BCx = r8;									// str r8, [sp, #12]

	r11 = By - Ay;								// sub r11, r5, r4
	r7 = DivTable(r11);							// ldr r7, [lr, r11, lsl #2]
	invHeightAB = r7;							// str r7, [sp, #108]
	r8 = Bx - Ax;								// sub r8, r2, r1
	r8 = r8 << 18;								// mov r8, r8, lsl #18
	r7 = ((int64_t)r8 * (int64_t)r7) >> 32;		// smull r8, r7, r8, r7
	ABdx = r7;									// str r7, [sp, #16]
	r10 = Ay & 7;								// and r10, r4, #7
	r10 = 8 - r10;								// rsb r10, r10, #8
	r8 = r7 * r10;								// mul r8, r7, r10
	r8 /= 8;									// mov r8, r8, asr #3
	r8 += Ax << 13;								// add r8, r8, r1, lsl #13
	ABx = r8;									// str r8, [sp, #20]

	r9 = Cy - Ay;								// sub r9, r6, r4
	r12 = DivTable(r9);							// ldr r12, [lr, r9, lsl #2]
	r8 = Cx - Ax;								// sub r8, r3, r1
	r8 <<= 18;									// mov r8, r8, lsl #18
	r9 = ((int64_t)r8 * (int64_t)r12) >> 32;	// smull r8, r9, r8, r12
	ACdx = r9;									// str r9, [sp, #24]
	r8 = r9 * r10;								// mul r8, r9, r10
	r8 /= 8;									// mov r8, r8, asr #3
	r8 += Ax << 13;								// add r8, r8, r1, lsl #13
	ACx = r8;									// str r8, [sp, #28]

	// reciprocal of widest row for best division precision
	r9 = r9 * r11;		// mul r9, r9, r11
	r9 += Ax << 16;		// add r9, r9, r1, lsl #16
	r9 -= Bx << 16;		// sub r9, r9, r2, lsl #16
	r9 >>= 12;			// mov r9, r9, asr #12
	r0 = DivTable(r9);	// ldr r0, [lr, r9, lsl #2]

	int32_t const heightAB = (By/8) - (Ay/8);
	int32_t const heightBC = (Cy/8) - (By/8);

	if (ABdx > ACdx)
	{
		// U deltas
		r1 = Au;									// ldrh r1, [sp, #88]
		r2 = Bu;									// ldrh r2, [sp, #92]
		r5 = Cu;									// ldrh r5, [sp, #96]
		r4 = r5 - r1;								// sub r4, r5, r1
		r4 = r4 << 13;								// mov r4, r4, lsl #13
		r4 = ((int64_t)r4 * (int64_t)r12) >> 32;	// smull r5, r4, r4, r12
		r9 = r4 * r11;								// mul r9, r4, r11
		r9 += r1 << 11;								// add r9, r9, r1, lsl #11
		r9 -= r2 << 11;								// sub r9, r9, r2, lsl #11
		r9 /= 4;									// mov r9, r9, asr #2
		r9 = ((int64_t)r9 * (int64_t)r0) >> 32; 	// smull r5, r9, r9, r0
		r3 = r4 * r10;								// mul r3, r4, r10
		r3 = r1 + (r3 >> 11);						// add r3, r1, r3, asr #11

		// V deltas
		r1 = Av;									// ldrh r1, [sp, #90]
		r2 = Bv;									// ldrh r2, [sp, #94]
		r5 = Cv;									// ldrh r5, [sp, #98]
		r6 = r5 - r1;								// sub r6, r5, r1
		r6 = r6 << 13;								// mov r6, r6, lsl #13
		r6 = ((int64_t)r6 * (int64_t)r12) >> 32;	// smull r5, r6, r6, r12
		r7 = r6 * r11;								// mul r7, r6, r11
		r7 += r1 << 11;								// add r7, r7, r1, lsl #11
		r7 -= r2 << 11;								// sub r7, r7, r2, lsl #11
		r7 /= 4;									// mov r7, r7, asr #2
		r7 = ((int64_t)r7 * (int64_t)r0) >> 32; 	// smull r5, r7, r7, r0
		r8 = r6 * r10;								// mul r8, r6, r10
		r8 = r1 + (r8 >> 11);						// add r8, r1, r8, asr #11

		// UV
		r3 = r8 | (r3 << 16);		// orr r3, r8, r3, lsl #16

		// UV per-row delta
		r4 >>= 8;					// mov r4, r4, asr #8
		r6 <<= 8;					// mov r6, r6, lsl #8
		r6 = (uint32_t)r6 >> 16;	// mov r6, r6, lsr #16
		r4 = r6 | (r4 << 16);		// orr r4, r6, r4, lsl #16

		// UV per-pixel delta
		r7 <<= 16;					// mov r7, r7, lsl #16
		r7 = (uint32_t)r7 >> 16;	// mov r7, r7, lsr #16
		r9 = r7 | (r9 << 16);		// orr r9, r7, r9, lsl #16

		if (heightAB != 0) {
			FillAffineTexTrapezoid(
				renderTarget, scale,
				ACx, ACdx,
				ABx, ABdx,
				Ay/8, heightAB,
				r3, r4, r9,
				cpu, texPtr
			);
		}

		if (heightBC != 0) {
			FillAffineTexTrapezoid(
				renderTarget, scale,
				ACx+heightAB*ACdx, ACdx,
				BCx, BCdx,
				By/8, heightBC,
				r3+heightAB*r4, r4, r9,
				cpu, texPtr
			);
		}
	}
	else
	{
		// U deltas
		r1 = Au;									// ldrh r1, [sp, #88]
		r2 = Bu;									// ldrh r2, [sp, #92]
		r5 = Cu;									// ldrh r5, [sp, #96]
		r4 = r5 - r1;								// sub r4, r5, r1
		r4 <<= 13;									// mov r4, r4, lsl #13
		r4 = ((int64_t)r4 * (int64_t)r12) >> 32;	// smull r5, r4, r4, r12
		r9 = r4 * r11;								// mul r9, r4, r11
		r9 += r1 << 11;								// add r9, r9, r1, lsl #11
		r9 -= r2 << 11;								// sub r9, r9, r2, lsl #11
		r9 /= 4;									// mov r9, r9, asr #2
		r9 = ((int64_t)r9 * (int64_t)r0) >> 32;		// smull r5, r9, r9, r0
		r4 = r2 - r1;								// sub r4, r2, r1
		r4 <<= 13;									// mov r4, r4, lsl #13
		r14 = invHeightAB;							// ldr lr, [sp, #108]
		r4 = ((int64_t)r4 * (int64_t)r14) >> 32;	// smull r5, r4, r4, lr
		r3 = r4 * r10;								// mul r3, r4, r10
		r3 = r1 + (r3 >> 11);						// add r3, r1, r3, asr #11

		// V deltas
		r1 = Av;									// ldrh r1, [sp, #90]
		r2 = Bv;									// ldrh r2, [sp, #94]
		r5 = Cv;									// ldrh r5, [sp, #98]
		r6 = r5 - r1;								// sub r6, r5, r1
		r6 <<= 13;									// mov r6, r6, lsl #13
		r6 = ((int64_t)r6 * (int64_t)r12) >> 32;	// smull r5, r6, r6, r12
		r7 = r6 * r11;								// mul r7, r6, r11
		r7 += r1 << 11;								// add r7, r7, r1, lsl #11
		r7 -= r2 << 11;								// sub r7, r7, r2, lsl #11
		r7 /= 4;									// mov r7, r7, asr #2
		r7 = ((int64_t)r7 * (int64_t)r0) >> 32;		// smull r5, r7, r7, r0
		r6 = r2 - r1;								// sub r6, r2, r1
		r6 <<= 13;									// mov r6, r6, lsl #13
		r6 = ((int64_t)r6 * (int64_t)r14) >> 32;	// smull r5, r6, r6, lr
		r8 = r6 * r10;								// mul r8, r6, r10
		r8 = r1 + (r8 >> 11);						// add r8, r1, r8, asr #11

		// UV per-pixel delta
		r7 <<= 16;					// mov r7, r7, lsl #16
		r7 = (uint32_t)r7 >> 16;	// mov r7, r7, lsr #16
		r9 = r7 | (r9 << 16);		// orr r9, r7, r9, lsl #16

		// UV
		r3 = r8 | (r3 << 16);		// orr r3, r8, r3, lsl #16

		// UV per-row delta
		r4 >>= 8;					// mov r4, r4, asr #8
		r6 <<= 8;					// mov r6, r6, lsl #8
		r6 = (uint32_t)r6 >> 16;	// mov r6, r6, lsr #16
		r4 = r6 | (r4 << 16);		// orr r4, r6, r4, lsl #16

		if (heightAB != 0) {
			FillAffineTexTrapezoid(
				renderTarget, scale,
				ABx, ABdx,
				ACx, ACdx,
				Ay/8, heightAB,
				r3, r4, r9,
				cpu, texPtr
			);
		}

		// U deltas
		r5 = Bu;									// ldrh r5, [sp, #92]
		r6 = Cu;									// ldrh r6, [sp, #96]
		r4 = r6 - r5;								// sub r4, r6, r5
		r4 <<= 13;									// mov r4, r4, lsl #13
		r14 = invHeightBC;							// ldr lr, [sp, #112]
		r4 = ((int64_t)r4 * (int64_t)r14) >> 32;	// smull r6, r4, r4, lr
		r0 = subpixelOffsetBC;						// ldrb r0, [sp, #116]
		r3 = r4 * r0;								// mul r3, r4, r0
		r3 = r5 + (r3 >> 11);						// add r3, r5, r3, asr #11

		// V deltas
		r7 = Bv;									// ldrh r7, [sp, #94]
		r6 = Cv;									// ldrh r6, [sp, #98]
		r6 -= r7;									// sub r6, r6, r7
		r6 <<= 13;									// mov r6, r6, lsl #13
		r6 = ((int64_t)r6 * (int64_t)r14) >> 32;	// smull r5, r6, r6, lr
		r8 = r6 * r0;								// mul r8, r6, r0
		r8 = r7 + (r8 >> 11);						// add r8, r7, r8, asr #11

		// UV
		r3 = r8 | (r3 << 16);		// orr r3, r8, r3, lsl #16

		// UV per-row delta
		r4 >>= 8;					// mov r4, r4, asr #8
		r6 <<= 8;					// mov r6, r6, lsl #8
		r6 = (uint32_t)r6 >> 16;	// mov r6, r6, lsr #16
		r4 = r6 | (r4 << 16);		// orr r4, r6, r4, lsl #16

		if (heightBC != 0) {
			FillAffineTexTrapezoid(
				renderTarget, scale,
				BCx, BCdx,
				ACx+heightAB*ACdx, ACdx,
				By/8, heightBC,
				r3, r4, r9,
				cpu, texPtr
			);
		}
	}
}

static void RasterizeColoredTri(struct HLE3DBackendDrome* backend, struct ARMCore* cpu, struct RenderParams const* params, uint8_t* renderTarget, uint32_t activeTriPtr)
{
	UNUSED(params);

	int const scale = backend->b.h->renderScale;
	uint8_t const colorIndex = cpu->memory.load8(cpu, activeTriPtr+11, NULL);

	int32_t const yx0 = cpu->memory.load32(cpu, activeTriPtr+12, NULL);
	int32_t const yx1 = cpu->memory.load32(cpu, activeTriPtr+16, NULL);
	int32_t const yx2 = cpu->memory.load32(cpu, activeTriPtr+20, NULL);

	FillColoredTri(renderTarget, scale, yx0, yx1, yx2, colorIndex);
}

static void RasterizeStaticTexTri(struct HLE3DBackendDrome* backend, struct ARMCore* cpu, struct RenderParams const* params, uint8_t* renderTarget, uint32_t activeTriPtr)
{
	UNUSED(backend);
	UNUSED(cpu);
	UNUSED(params);
	UNUSED(activeTriPtr);
	UNUSED(renderTarget);
}

static void RasterizeAffineTexTri(struct HLE3DBackendDrome* backend, struct ARMCore* cpu, struct RenderParams const* params, uint8_t* renderTarget, uint32_t activeTriPtr)
{
	uint8_t const texIndex = cpu->memory.load8(cpu, activeTriPtr+11, NULL);
	uint32_t const texPtr = params->baseTexPtr + (texIndex << 16);
	int const scale = backend->b.h->renderScale;

	int32_t const yx0 = cpu->memory.load32(cpu, activeTriPtr+12, NULL);
	int32_t const yx1 = cpu->memory.load32(cpu, activeTriPtr+20, NULL);
	int32_t const yx2 = cpu->memory.load32(cpu, activeTriPtr+28, NULL);
	int32_t const vu0 = cpu->memory.load32(cpu, activeTriPtr+16, NULL);
	int32_t const vu1 = cpu->memory.load32(cpu, activeTriPtr+24, NULL);
	int32_t const vu2 = cpu->memory.load32(cpu, activeTriPtr+32, NULL);

	FillAffineTexTri(
		renderTarget, scale,
		yx0, yx1, yx2,
		vu0, vu1, vu2,
		texPtr,
		cpu);
}

static void RasterizeColoredTriClipped(struct HLE3DBackendDrome* backend, struct ARMCore* cpu, struct RenderParams const* params, uint8_t* renderTarget, uint32_t activeTriPtr)
{
	uint8_t const clipFlags = cpu->memory.load8(cpu, activeTriPtr+1, NULL);

	int32_t vertBuffer[64];
	vertBuffer[0] = cpu->memory.load32(cpu, activeTriPtr+12, NULL);
	vertBuffer[1] = cpu->memory.load32(cpu, activeTriPtr+16, NULL);
	vertBuffer[2] = cpu->memory.load32(cpu, activeTriPtr+20, NULL);

	int32_t vertCount = ClipColoredEdgePolygon(params, clipFlags, vertBuffer);

	if (vertCount < 3)
		return;

	int const scale = backend->b.h->renderScale;
	uint8_t const colorIndex = cpu->memory.load8(cpu, activeTriPtr+11, NULL);

	vertCount -= 2;
	int32_t yx0 = vertBuffer[0];
	int32_t yx1 = vertBuffer[vertCount];
	int32_t yx2 = vertBuffer[vertCount+1];

	do {
		FillColoredTri(renderTarget, scale, yx0, yx1, yx2, colorIndex);

		vertCount--;
		yx0 = vertBuffer[0];
		yx1 = vertBuffer[vertCount];
		yx2 = vertBuffer[vertCount+1];
	} while (vertCount != 0);
}

static void RasterizeStaticTexTriClipped(struct HLE3DBackendDrome* backend, struct ARMCore* cpu, struct RenderParams const* params, uint8_t* renderTarget, uint32_t activeTriPtr)
{
	UNUSED(backend);
	UNUSED(cpu);
	UNUSED(params);
	UNUSED(renderTarget);
	UNUSED(activeTriPtr);
}

static void RasterizeAffineTexTriClipped(struct HLE3DBackendDrome* backend, struct ARMCore* cpu, struct RenderParams const* params, uint8_t* renderTarget, uint32_t activeTriPtr)
{
	uint8_t const clipFlags = cpu->memory.load8(cpu, activeTriPtr+1, NULL);

	int32_t vertBuffer[64];
	vertBuffer[0] = cpu->memory.load32(cpu, activeTriPtr+12, NULL);
	vertBuffer[1] = cpu->memory.load32(cpu, activeTriPtr+16, NULL);
	vertBuffer[2] = cpu->memory.load32(cpu, activeTriPtr+20, NULL);
	vertBuffer[3] = cpu->memory.load32(cpu, activeTriPtr+24, NULL);
	vertBuffer[4] = cpu->memory.load32(cpu, activeTriPtr+28, NULL);
	vertBuffer[5] = cpu->memory.load32(cpu, activeTriPtr+32, NULL);

	int32_t vertCount = ClipAffineTexEdgePolygon(params, clipFlags, vertBuffer);

	if (vertCount < 3)
		return;

	uint8_t const texIndex = cpu->memory.load8(cpu, activeTriPtr+11, NULL);
	uint32_t const texPtr = params->baseTexPtr + (texIndex << 16);
	int const scale = backend->b.h->renderScale;

	vertCount -= 2;
	int32_t yx0 = vertBuffer[0];
	int32_t yx1 = vertBuffer[vertCount*2];
	int32_t yx2 = vertBuffer[(vertCount+1)*2];
	int32_t vu0 = vertBuffer[1];
	int32_t vu1 = vertBuffer[vertCount*2+1];
	int32_t vu2 = vertBuffer[(vertCount+1)*2+1];

	do {
		FillAffineTexTri(
			renderTarget, scale,
			yx0, yx1, yx2,
			vu0, vu1, vu2,
			texPtr, cpu);

		vertCount--;
		yx0 = vertBuffer[0];
		yx1 = vertBuffer[vertCount*2];
		yx2 = vertBuffer[(vertCount+1)*2];
		vu0 = vertBuffer[1];
		vu1 = vertBuffer[vertCount*2+1];
		vu2 = vertBuffer[(vertCount+1)*2+1];
	} while (vertCount != 0);
}

static void RasterizeSpriteOccluder(struct HLE3DBackendDrome* backend, struct ARMCore* cpu, struct RenderParams const* params, uint8_t* renderTarget, uint32_t activeTriPtr)
{
	uint8_t const spriteIndex = cpu->memory.load8(cpu, activeTriPtr+8, NULL);

	uint32_t const ptrSpriteParams = params->ptrSpriteParamsTable + spriteIndex*8;

	int16_t const spriteX = cpu->memory.load16(cpu, ptrSpriteParams+2, NULL);
	int16_t const spriteY = cpu->memory.load16(cpu, ptrSpriteParams+4, NULL);

	if (spriteX >= params->clipLeft / 8 &&
		spriteX <  params->clipRight / 8 &&
		spriteY >= params->clipTop / 8 &&
		spriteY <  params->clipBottom / 8)
	{
		int const scale = backend->b.h->renderScale;
		int const stride = 240*scale;
		int const x = (spriteX*scale);
		int const y = (spriteY*scale);

		uint8_t const pixel = renderTarget[y*stride+x];
		cpu->memory.store16(cpu, ptrSpriteParams+6, pixel, NULL);
		renderTarget[y*stride+x] = 0;

		backend->spriteStack[backend->spriteStackHeight++] = spriteIndex;
	}
	else
	{
		cpu->memory.store16(cpu, ptrSpriteParams+6, 1, NULL);
	}
}

static void FinalizeSpriteOccluders(struct HLE3DBackendDrome* backend, struct ARMCore* cpu, struct RenderParams const* params, uint8_t* renderTarget)
{
	int const scale = backend->b.h->renderScale;
	int const stride = 240*scale;

	while (backend->spriteStackHeight > 0)
	{
		uint8_t const spriteIndex = backend->spriteStack[--backend->spriteStackHeight];

		uint32_t const ptrSpriteParams = params->ptrSpriteParamsTable + spriteIndex*8;

		int16_t const spriteX = cpu->memory.load16(cpu, ptrSpriteParams+2, NULL);
		int16_t const spriteY = cpu->memory.load16(cpu, ptrSpriteParams+4, NULL);

		int const x = (spriteX*scale);
		int const y = (spriteY*scale);

		uint8_t const pixel = renderTarget[y*stride+x];
		if (pixel == 0)
		{
			uint8_t const originalPixel = cpu->memory.load16(cpu, ptrSpriteParams+6, NULL);
			renderTarget[y*stride+x] = originalPixel;
			cpu->memory.store16(cpu, ptrSpriteParams+6, 2, NULL);
		}
		else
		{
			cpu->memory.store16(cpu, ptrSpriteParams+6, 0, NULL);
		}
	}
}
