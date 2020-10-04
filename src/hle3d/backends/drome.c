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

static const uint32_t
	kIdentDromeEU             = 0x58454f41, // AOEX
	kIdentDromeNA             = 0x45454f41, // AOEE
	kIdentHotWheelsStuntTrack = 0x45454842, // BHEE
	kIdentHotWheels2Pack      = 0x454a5142; // BQJE

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
	dromeBackend->disableRealRasterizer = false;//true;

	if (ident == kIdentDromeEU || ident == kIdentDromeNA)
	{
		dromeBackend->addrRamExecutionPoint    = 0x030023d4;
		dromeBackend->addrRamActiveFunctionPtr = 0x03000e04;
		dromeBackend->addrRomTransformFunc     = 0x08064880;
		dromeBackend->addrRomRasterizeFunc     = 0x08000330;
		//if (ident == kIdentDromeEU)
		//{
		//	dromeBackend->addrRomJumpToTransform = 0x0807043C;
		//	dromeBackend->addrRomJumpToRasterize = 0x08070454;
		//}
		//if (ident == kIdentDromeNA)
		//{
		//	dromeBackend->addrRomJumpToTransform = 0x08070408;
		//	dromeBackend->addrRomJumpToRasterize = 0x08070420;
		//}
	}
	else if (ident == kIdentHotWheelsStuntTrack)
	{
		dromeBackend->addrRamExecutionPoint    = 0x0300243c;
		dromeBackend->addrRamActiveFunctionPtr = 0x03000dd8;
		dromeBackend->addrRomTransformFunc     = 0x080ea350;
		dromeBackend->addrRomRasterizeFunc     = 0x08085e00;
		//dromeBackend->addrRomJumpToTransform   = 0x080f3b4c;
		//dromeBackend->addrRomJumpToRasterize   = 0x080f3b64;
	}
	else if (ident == kIdentHotWheels2Pack)
	{
		dromeBackend->addrRamExecutionPoint    = 0x03002294;
		dromeBackend->addrRamActiveFunctionPtr = 0x03000dd8;
		dromeBackend->addrRomTransformFunc     = 0x088ea350;
		dromeBackend->addrRomRasterizeFunc     = 0x08885e00;
		//dromeBackend->addrRomJumpToTransform   = 0x088f3b4c;
		//dromeBackend->addrRomJumpToRasterize   = 0x088f3b64;
	}

	HLE3DAddBreakpoint(hle3d, dromeBackend->addrRamExecutionPoint);
}

void HLE3DBackendDromeDeinit(struct HLE3DBackend* backend)
{
	struct HLE3DBackendDrome* dromeBackend = (struct HLE3DBackendDrome*)backend;

	int const scale = dromeBackend->b.h->renderScale;

	FILE* fh = fopen("/Users/will/drome-screen.bin", "wb");
	fwrite(dromeBackend->b.h->backgroundMode4[0], 1, 240*160*scale*scale, fh);
	fclose(fh);
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
	//printf("active ram function: %08x\n", activeRamFunctionPtr);

	if (activeRamFunctionPtr == dromeBackend->addrRomTransformFunc) {
		HLE3DBackendDromeHookTransform(dromeBackend, cpu);
	}

	if (activeRamFunctionPtr == dromeBackend->addrRomRasterizeFunc) {
		HLE3DBackendDromeHookRasterizer(dromeBackend, cpu);
	}
}

void HLE3DBackendDromeHookTransform(struct HLE3DBackendDrome* backend, struct ARMCore* cpu)
{
	//printf("---------\n");
	//printf("hooked the transform pipeline!\n");
	uint32_t const r0 = cpu->gprs[0];
	//printf("r0: %08x\n", r0);
	uint32_t const objectsPtr = cpu->memory.load32(cpu, r0+40, NULL);
	//printf("pObjects: %08x\n", objectsPtr);

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
	int32_t const camPosX = cpu->memory.load32(cpu, r0+20, NULL);
	int32_t const camPosY = cpu->memory.load32(cpu, r0+24, NULL);
	int32_t const camPosZ = cpu->memory.load32(cpu, r0+28, NULL);
	//printf(
	//	"camPos: (%.2f,%.2f,%.2f)\n",
	//	camPosX/4096.0f,
	//	camPosY/4096.0f,
	//	camPosZ/4096.0f);

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

void HLE3DBackendDromeHookRasterizer(struct HLE3DBackendDrome* backend, struct ARMCore* cpu)
{
	//printf("---------\n");
	//printf("hooked the raster pipeline!\n");
	uint32_t const r0 = cpu->gprs[0];
	//printf("r0: %08x\n", r0);
	uint32_t const renderStreamPtr = cpu->memory.load32(cpu, r0+56, NULL);
	//printf("renderStreamPtr: %08x\n", renderStreamPtr);
	uint32_t const drawBuffer = cpu->memory.load32(cpu, r0+60, NULL);
	//printf("drawBuffer: %08x\n", drawBuffer);

	int const scale = backend->b.h->renderScale;
	uint8_t* renderTarget = backend->b.h->backgroundMode4[(drawBuffer == 0x06000000) ? 0 : 1];
	memset(renderTarget, 0, 240*160*scale*scale);

	uint32_t numTrianglesTotal = 0;
	uint32_t numTriangles[8] = {0};
	uint32_t activeTriPtr = renderStreamPtr;
	uint8_t activeTriType = cpu->memory.load8(cpu, activeTriPtr, NULL);
	while (activeTriType != 0) {
		if (activeTriType > 7) {
			printf("bad tri type %d\n", activeTriType);
			break;
		}
		numTrianglesTotal++;
		numTriangles[activeTriType]++;

		switch (activeTriType)
		{
			case 1:
			case 4:
				HLE3DBackendDromeRasterizeFlatTri(backend, cpu, activeTriPtr, renderTarget);
				break;
			case 2:
			case 5:
				HLE3DBackendDromeRasterizeStaticTexTri(backend, cpu, activeTriPtr, renderTarget);
				break;
			case 3:
			case 6:
				HLE3DBackendDromeRasterizeAffineTexTri(backend, cpu, activeTriPtr, renderTarget);
				break;
			case 7:
				HLE3DBackendDromeRasterizeSpriteOccluder(backend, cpu, activeTriPtr, renderTarget);
				break;
		}

		uint16_t const nextTriPtr = cpu->memory.load16(cpu, activeTriPtr+2, NULL);
		activeTriPtr = (activeTriPtr & 0xffff0000) | nextTriPtr;
		activeTriType = cpu->memory.load8(cpu, activeTriPtr, NULL);
	}
	//printf("%d total primitives\n", numTrianglesTotal);
	//if (numTriangles[1]) { printf("  %d flat\n",               numTriangles[1]); }
	//if (numTriangles[2]) { printf("  %d static tex\n",         numTriangles[2]); }
	//if (numTriangles[3]) { printf("  %d affine tex\n",         numTriangles[3]); }
	//if (numTriangles[4]) { printf("  %d clipped flat\n",       numTriangles[4]); }
	//if (numTriangles[5]) { printf("  %d clipped static tex\n", numTriangles[5]); }
	//if (numTriangles[6]) { printf("  %d clipped affine tex\n", numTriangles[6]); }
	//if (numTriangles[7]) { printf("  %d sprite occluders\n",   numTriangles[7]); }

	if (backend->disableRealRasterizer) {
		// disable the real rasterizer
		// by making the first primitive the "end" primitive
		cpu->memory.store8(cpu, renderStreamPtr, 0, NULL);
	}
}

void HLE3DBackendDromeRasterizeFlatTri(struct HLE3DBackendDrome* backend, struct ARMCore* cpu, uint32_t activeTriPtr, uint8_t* renderTarget)
{
	uint8_t const triType = cpu->memory.load8(cpu, activeTriPtr+0, NULL);
	uint8_t const clipFlags = cpu->memory.load8(cpu, activeTriPtr+1, NULL);
	uint8_t const colorIndex = cpu->memory.load8(cpu, activeTriPtr+11, NULL);
	int16_t const x0 = cpu->memory.load16(cpu, activeTriPtr+12, NULL);
	int16_t const y0 = cpu->memory.load16(cpu, activeTriPtr+14, NULL);
	int16_t const x1 = cpu->memory.load16(cpu, activeTriPtr+16, NULL);
	int16_t const y1 = cpu->memory.load16(cpu, activeTriPtr+18, NULL);
	int16_t const x2 = cpu->memory.load16(cpu, activeTriPtr+20, NULL);
	int16_t const y2 = cpu->memory.load16(cpu, activeTriPtr+22, NULL);

	int const scale = backend->b.h->renderScale;

	if (triType == 1)
	{
		int const stride = 240*scale;

		int const ix0 = x0, iy0 = y0;
		int const ix1 = x1, iy1 = y1;
		int const ix2 = x2, iy2 = y2;

		renderTarget[((iy0*scale)/8)*stride+((ix0*scale)/8)] = colorIndex;
		renderTarget[((iy1*scale)/8)*stride+((ix1*scale)/8)] = colorIndex;
		renderTarget[((iy2*scale)/8)*stride+((ix2*scale)/8)] = colorIndex;

		//for (int i=1;i<16;++i) {
		//	int x = ((ix0*i)+(ix1*(16-i)))/16;
		//	int y = ((iy0*i)+(iy1*(16-i)))/16;
		//	renderTarget[((y*scale)/8)*stride+((x*scale)/8)] = colorIndex;
		//}
		//for (int i=1;i<16;++i) {
		//	int x = ((ix1*i)+(ix2*(16-i)))/16;
		//	int y = ((iy1*i)+(iy2*(16-i)))/16;
		//	renderTarget[((y*scale)/8)*stride+((x*scale)/8)] = colorIndex;
		//}
		//for (int i=1;i<16;++i) {
		//	int x = ((ix0*i)+(ix2*(16-i)))/16;
		//	int y = ((iy0*i)+(iy2*(16-i)))/16;
		//	renderTarget[((y*scale)/8)*stride+((x*scale)/8)] = colorIndex;
		//}
	}

	//printf(
	//	"flat tri - type %02x, clip %02x, color %02x, (%d,%d) (%d,%d) (%d,%d)\n",
	//	triType,
	//	clipFlags,
	//	colorIndex,
	//	x0/8, y0/8,
	//	x1/8, y1/8,
	//	x2/8, y2/8);
}

void HLE3DBackendDromeRasterizeStaticTexTri(struct HLE3DBackendDrome* backend, struct ARMCore* cpu, uint32_t activeTriPtr, uint8_t* renderTarget)
{
	UNUSED(cpu);
	UNUSED(activeTriPtr);
}

void HLE3DBackendDromeRasterizeAffineTexTri(struct HLE3DBackendDrome* backend, struct ARMCore* cpu, uint32_t activeTriPtr, uint8_t* renderTarget)
{
	uint8_t const triType = cpu->memory.load8(cpu, activeTriPtr+0, NULL);
	uint8_t const clipFlags = cpu->memory.load8(cpu, activeTriPtr+1, NULL);
	uint8_t const texIndex = cpu->memory.load8(cpu, activeTriPtr+11, NULL);
	int16_t const x0 = cpu->memory.load16(cpu, activeTriPtr+12, NULL);
	int16_t const y0 = cpu->memory.load16(cpu, activeTriPtr+14, NULL);
	int16_t const u0 = cpu->memory.load16(cpu, activeTriPtr+16, NULL);
	int16_t const v0 = cpu->memory.load16(cpu, activeTriPtr+18, NULL);
	int16_t const x1 = cpu->memory.load16(cpu, activeTriPtr+20, NULL);
	int16_t const y1 = cpu->memory.load16(cpu, activeTriPtr+22, NULL);
	int16_t const u1 = cpu->memory.load16(cpu, activeTriPtr+24, NULL);
	int16_t const v1 = cpu->memory.load16(cpu, activeTriPtr+26, NULL);
	int16_t const x2 = cpu->memory.load16(cpu, activeTriPtr+28, NULL);
	int16_t const y2 = cpu->memory.load16(cpu, activeTriPtr+30, NULL);
	int16_t const u2 = cpu->memory.load16(cpu, activeTriPtr+32, NULL);
	int16_t const v2 = cpu->memory.load16(cpu, activeTriPtr+34, NULL);
	//printf(
	//	"affine tex tri - type %02x, clip %02x, tex %02x, (%d,%d) (%d,%d) (%d,%d)\n",
	//	triType,
	//	clipFlags,
	//	texIndex,
	//	x0/8, y0/8,
	//	x1/8, y1/8,
	//	x2/8, y2/8);
	
	UNUSED(triType);
	UNUSED(clipFlags);
	UNUSED(texIndex);
	UNUSED(x0); UNUSED(y0);	UNUSED(u0); UNUSED(v0);
	UNUSED(x1); UNUSED(y1);	UNUSED(u1); UNUSED(v1);
	UNUSED(x2); UNUSED(y2);	UNUSED(u2); UNUSED(v2);
}

void HLE3DBackendDromeRasterizeSpriteOccluder(struct HLE3DBackendDrome* backend, struct ARMCore* cpu, uint32_t activeTriPtr, uint8_t* renderTarget)
{
	uint8_t const spriteIndex = cpu->memory.load8(cpu, activeTriPtr+8, NULL);

	uint32_t const r0 = cpu->gprs[0];

	uint16_t const viewportX      = cpu->memory.load16(cpu, r0+32, NULL);
	uint16_t const viewportY      = cpu->memory.load16(cpu, r0+34, NULL);
	uint16_t const viewportWidth  = cpu->memory.load16(cpu, r0+36, NULL);
	uint16_t const viewportHeight = cpu->memory.load16(cpu, r0+38, NULL);

	uint16_t const clipLeft   = (viewportX*8)+4;
	uint16_t const clipTop    = (viewportY*8)+4;
	uint16_t const clipRight  = clipLeft + (viewportWidth*8);
	uint16_t const clipBottom = clipTop + (viewportHeight*8);

	uint32_t const ptrSpriteParamsTable = r0+96;
	uint32_t const ptrSpriteParams = ptrSpriteParamsTable + spriteIndex*8;

	int16_t const spriteX = cpu->memory.load16(cpu, ptrSpriteParams+2, NULL);
	int16_t const spriteY = cpu->memory.load16(cpu, ptrSpriteParams+4, NULL);

	//printf("sprite occluder - sprite %02x, (%d,%d)\n", spriteIndex, spriteX, spriteY);

	cpu->memory.store16(cpu, ptrSpriteParams+6, 2, NULL);

	UNUSED(spriteX);
	UNUSED(spriteY);
	UNUSED(clipLeft);
	UNUSED(clipTop);
	UNUSED(clipRight);
	UNUSED(clipBottom);
}
