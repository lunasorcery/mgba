/*
High-Level Emulation of 3D engine from:
- V-Rally 3 (2002)
- Stuntman (2003)
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
	kIdentVRally3EU           = 0x50525641, // AVRP
	kIdentVRally3JP           = 0x4a525641, // AVRJ
	kIdentVRally3NA           = 0x45525641, // AVRE
	kIdentStuntmanEU          = 0x50585541, // AUXP
	kIdentStuntmanNA          = 0x45585541, // AUXE
	kIdentVRally3Stuntman2in1 = 0x50534342, // BCSP
	kIdentAsterixXXL          = 0x50584c42, // BLXP
	kIdentAsterixXXL2in1      = 0x50413242, // B2AP
	kIdentDriv3rEU            = 0x50523342, // B3RP
	kIdentDriv3rNA            = 0x45523342; // B3RE

static bool const kDebugPrint = false;
static bool const kDebugDraw = false;

struct RenderParams {
	uint8_t frontBufferIndex;
	uint8_t backBufferIndex;
	int scale;
	int rtWidth;
	int rtHeight;
	int rtTotalPixels;
};

static void SetupBreakpoints(struct HLE3DBackendV3D* backend);

static void ClearScreen(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params);
static void CopyScreen(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params);
static void FlipBuffers(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, uint32_t pc);
static void FillColoredTrapezoid(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params);
static void FillTexturedTrapezoid(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params, uint32_t addrUv0, uint32_t addrUv1, uint32_t addrUvRowDelta0, uint32_t addrUvRowDelta1);

static void DrawVRally3ScaledEnvSprite(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params);
static void DrawVRally3VehicleInterior(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params);
static void DrawVRally3VehicleSprite(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params);
static void DrawVRally3Text(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params);

static void DrawStuntmanSprite0(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params);
static void DrawStuntmanSprite1(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params);

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

	v3dBackend->ident = ident;

	// zero out the game-specific values
	v3dBackend->isVRally3 = false;
	v3dBackend->addrFuncVRally3ScaledEnvSprite = 0;
	v3dBackend->addrFuncVRally3VehicleSprite = 0;
	v3dBackend->addrFuncVRally3DrawText = 0;
	v3dBackend->addrVRally3VehicleSpriteStride = 0;

	v3dBackend->isStuntman = false;
	v3dBackend->addrFuncStuntmanSprite0 = 0;
	v3dBackend->addrFuncStuntmanSprite1 = 0;

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

	v3dBackend->addrFuncAsterix2in1GameSelection = 0;
	v3dBackend->addrFuncVRally3Stuntman2in1GameSelection = 0;

	if (ident == kIdentVRally3EU || ident == kIdentVRally3JP || ident == kIdentVRally3NA)
	{
		v3dBackend->isVRally3 = true;
		SetupBreakpoints(v3dBackend);
	}
	else if (ident == kIdentStuntmanEU || ident == kIdentStuntmanNA)
	{
		v3dBackend->isStuntman = true;
		SetupBreakpoints(v3dBackend);
	}
	else if (ident == kIdentAsterixXXL)
	{
		v3dBackend->isAsterix = true;
		SetupBreakpoints(v3dBackend);
	}
	else if (ident == kIdentDriv3rEU || ident == kIdentDriv3rEU)
	{
		v3dBackend->isDriv3r = true;
		SetupBreakpoints(v3dBackend);
	}
	else if (ident == kIdentVRally3Stuntman2in1)
	{
		v3dBackend->addrFuncVRally3Stuntman2in1GameSelection = 0x08000638;
		HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncVRally3Stuntman2in1GameSelection);
	}
	else if (ident == kIdentAsterixXXL2in1)
	{
		v3dBackend->addrFuncAsterix2in1GameSelection = 0x0885037c;
		HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncAsterix2in1GameSelection);
	}
}

void HLE3DBackendV3DDeinit(struct HLE3DBackend* backend)
{
	UNUSED(backend);
}

bool HLE3DBackendV3DIsGame(uint32_t ident)
{
	return
		(ident == kIdentVRally3EU) ||
		(ident == kIdentVRally3JP) ||
		(ident == kIdentVRally3NA) ||
		(ident == kIdentStuntmanEU) ||
		(ident == kIdentStuntmanNA) ||
		(ident == kIdentVRally3Stuntman2in1) ||
		(ident == kIdentAsterixXXL) ||
		(ident == kIdentAsterixXXL2in1) ||
		(ident == kIdentDriv3rEU) ||
		(ident == kIdentDriv3rNA);
}

void HLE3DBackendV3DHook(struct HLE3DBackend* backend, struct ARMCore* cpu, uint32_t pc)
{
	struct HLE3DBackendV3D* v3dBackend = (struct HLE3DBackendV3D*)backend;

	if (pc == v3dBackend->addrFuncVRally3Stuntman2in1GameSelection) {
		if (cpu->gprs[0] == 0) {
			v3dBackend->isVRally3 = true;
		} else {
			v3dBackend->isStuntman = true;
		}
		HLE3DClearBreakpoints(v3dBackend->b.h);
		SetupBreakpoints(v3dBackend);
		return;
	}

	if (pc == v3dBackend->addrFuncAsterix2in1GameSelection) {
		if (cpu->gprs[0] != 0) {
			v3dBackend->isAsterix = true;
			HLE3DClearBreakpoints(v3dBackend->b.h);
			SetupBreakpoints(v3dBackend);
		}
		return;
	}

	// prepare some parameters
	struct RenderParams params;
	uint8_t const frame = cpu->memory.load8(cpu, v3dBackend->addrActiveFrame, NULL);
	params.frontBufferIndex = frame?1:0;
	params.backBufferIndex = frame?0:1;
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
	if (pc == v3dBackend->addrFuncFlipBuffers ||
	    pc == v3dBackend->addrFuncFlipBuffers2 ||
	    pc == v3dBackend->addrFuncFlipBuffers3) {
		FlipBuffers(v3dBackend, cpu, pc);
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

	if (v3dBackend->isVRally3)
	{
		if (pc == v3dBackend->addrFuncVRally3ScaledEnvSprite) {
			DrawVRally3ScaledEnvSprite(v3dBackend, cpu, &params);
			return;
		}

		if (pc == v3dBackend->addrFuncVRally3VehicleInterior) {
			DrawVRally3VehicleInterior(v3dBackend, cpu, &params);
			return;
		}

		if (pc == v3dBackend->addrFuncVRally3VehicleSprite) {
			DrawVRally3VehicleSprite(v3dBackend, cpu, &params);
			return;
		}

		if (pc == v3dBackend->addrFuncVRally3DrawText) {
			DrawVRally3Text(v3dBackend, cpu, &params);
			return;
		}
	}

	if (v3dBackend->isStuntman)
	{
		if (pc == v3dBackend->addrFuncStuntmanSprite0) {
			DrawStuntmanSprite0(v3dBackend, cpu, &params);
			return;
		}

		if (pc == v3dBackend->addrFuncStuntmanSprite1) {
			DrawStuntmanSprite1(v3dBackend, cpu, &params);
			return;
		}
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

static void SetupBreakpoints(struct HLE3DBackendV3D* v3dBackend)
{
	struct HLE3D* hle3d = v3dBackend->b.h;

	if (v3dBackend->isVRally3)
	{
		// shared

		v3dBackend->addrFuncClearScreen  = 0x03003318;
		v3dBackend->addrFuncCopyScreen   = 0x0300625c;
		v3dBackend->addrScreenCopySource = 0x030062ec;
		v3dBackend->addrFuncFlipBuffers  = 0x0300779c;
		v3dBackend->addrFuncFlipBuffers2 = 0x030077dc;
		v3dBackend->addrFuncFlipBuffers3 = 0x030077a4;
		v3dBackend->addrActiveFrame      = 0x02038ac5;

		v3dBackend->addrFuncTexture1pxTrapezoid = 0x03003aa8;
		v3dBackend->addrTex1UvRowDelta0 = 0x03003a9c;
		v3dBackend->addrTex1UvRowDelta1 = 0x03003a94;
		v3dBackend->addrTex1Uv0         = 0x03003a98;
		v3dBackend->addrTex1Uv1         = 0x03003a90;

		v3dBackend->addrFuncTexture2pxTrapezoid = 0;
		v3dBackend->addrTex2UvRowDelta0 = 0;
		v3dBackend->addrTex2UvRowDelta1 = 0;
		v3dBackend->addrTex2Uv0         = 0;
		v3dBackend->addrTex2Uv1         = 0;

		v3dBackend->addrFuncColoredTrapezoid = 0x03003884;

		// game-specific

		v3dBackend->addrFuncVRally3ScaledEnvSprite = 0x03003554;
		v3dBackend->addrFuncVRally3VehicleInterior = 0x03006b58;
		v3dBackend->addrVRally3VehicleSpriteStride = 0x03004d9c;

		// VehicleSprite calls into 03004B60
		// DrawText calls into 03006DE8
		// we can't hook there directly because they're loops
		if (v3dBackend->ident == kIdentVRally3EU)
		{
			v3dBackend->addrFuncVRally3VehicleSprite = 0x08007708;
			v3dBackend->addrFuncVRally3DrawText = 0x08033684;
		}
		if (v3dBackend->ident == kIdentVRally3JP)
		{
			v3dBackend->addrFuncVRally3VehicleSprite = 0x08007704;
			v3dBackend->addrFuncVRally3DrawText = 0x08033688;
		}
		if (v3dBackend->ident == kIdentVRally3NA)
		{
			v3dBackend->addrFuncVRally3VehicleSprite = 0x08007720;
			v3dBackend->addrFuncVRally3DrawText = 0x080336A4;
		}
		if (v3dBackend->ident == kIdentVRally3Stuntman2in1)
		{
			v3dBackend->addrFuncVRally3VehicleSprite = 0x08407708;
			v3dBackend->addrFuncVRally3DrawText = 0x08433684;
		}

		HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncVRally3ScaledEnvSprite);
		HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncVRally3VehicleInterior);
		HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncVRally3VehicleSprite);
		HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncVRally3DrawText);
	}
	else if (v3dBackend->isStuntman)
	{
		// shared

		v3dBackend->addrFuncClearScreen  = 0x03004d88;
		v3dBackend->addrFuncCopyScreen   = 0x03006458;
		v3dBackend->addrScreenCopySource = 0x030064ec;
		v3dBackend->addrFuncFlipBuffers  = 0x03007280;
		v3dBackend->addrFuncFlipBuffers2 = 0x030072b8;
		v3dBackend->addrFuncFlipBuffers3 = 0;
		v3dBackend->addrActiveFrame      = 0x02038e0f;

		v3dBackend->addrFuncTexture1pxTrapezoid = 0x0300591c;
		v3dBackend->addrTex1UvRowDelta0 = 0x030052d4;
		v3dBackend->addrTex1UvRowDelta1 = 0x030052d8;
		v3dBackend->addrTex1Uv0         = 0x030052dc;
		v3dBackend->addrTex1Uv1         = 0x030052e0;

		v3dBackend->addrFuncTexture2pxTrapezoid = 0x030054e4;
		v3dBackend->addrTex2UvRowDelta0 = 0x030052d4;
		v3dBackend->addrTex2UvRowDelta1 = 0x030052d8;
		v3dBackend->addrTex2Uv0         = 0x030052dc;
		v3dBackend->addrTex2Uv1         = 0x030052e0;

		v3dBackend->addrFuncColoredTrapezoid = 0x030050a0;

		// game-specific

		v3dBackend->addrFuncStuntmanSprite0 = 0x03005ffc;
		v3dBackend->addrFuncStuntmanSprite1 = 0x030061e0;

		HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncStuntmanSprite0);
		HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncStuntmanSprite1);
	}
	else if (v3dBackend->isAsterix)
	{
		// shared

		v3dBackend->addrFuncClearScreen  = 0x03004198;
		v3dBackend->addrFuncCopyScreen   = 0x03006834;
		v3dBackend->addrScreenCopySource = 0x03006a00;
		v3dBackend->addrFuncFlipBuffers  = 0x030075b8;
		v3dBackend->addrFuncFlipBuffers2 = 0;
		v3dBackend->addrFuncFlipBuffers3 = 0;
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

		v3dBackend->addrFuncColoredTrapezoid = 0x030044f0;

		// game-specific

		v3dBackend->addrFuncAsterixPlayerSprite0   = 0x03005e0c;
		v3dBackend->addrFuncAsterixPlayerSprite1   = 0x03005f98;
		v3dBackend->addrFuncAsterixScaledEnvSprite = 0x03006144;
		v3dBackend->addrFuncAsterixScaledNpcSprite = 0x03006328;

		if (v3dBackend->ident == kIdentAsterixXXL)
		{
			v3dBackend->addrFuncAsterixMenuOverlay = 0x0805c5f0;
		}
		if (v3dBackend->ident == kIdentAsterixXXL2in1)
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
	else if (v3dBackend->isDriv3r)
	{
		// shared

		v3dBackend->addrFuncClearScreen  = 0x03004984;
		v3dBackend->addrFuncCopyScreen   = 0x03004a98;
		v3dBackend->addrScreenCopySource = 0x03004b2c;
		v3dBackend->addrFuncFlipBuffers  = 0x030078c0;
		v3dBackend->addrFuncFlipBuffers2 = 0;
		v3dBackend->addrFuncFlipBuffers3 = 0;
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

		v3dBackend->addrFuncColoredTrapezoid = 0x03004cb0;

		// game-specific

		v3dBackend->addrFuncDriv3rPlayerSprite = 0x030063d4;
		v3dBackend->addrFuncDriv3rScaledSprite = 0x030061e4;

		HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncDriv3rPlayerSprite);
		HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncDriv3rScaledSprite);
	}

	HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncClearScreen);
	HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncCopyScreen);
	HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncFlipBuffers);
	HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncFlipBuffers2);
	HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncFlipBuffers3);
	HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncColoredTrapezoid);
	HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncTexture1pxTrapezoid);
	HLE3DAddBreakpoint(hle3d, v3dBackend->addrFuncTexture2pxTrapezoid);
}

static void ClearScreen(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params)
{
	UNUSED(cpu);

	if (kDebugPrint) {
		printf("[HLE3D/V3D] ---- clear screen %d ----\n", params->backBufferIndex);
	}

	backend->b.h->bgMode4active[params->backBufferIndex] = false;

	memset(backend->b.h->bgMode4pal[params->backBufferIndex], 0, params->rtTotalPixels);
}

static void CopyScreen(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params)
{
	backend->b.h->bgMode4active[params->backBufferIndex] = false;

	uint32_t const srcAddr = cpu->memory.load32(cpu, backend->addrScreenCopySource, NULL);

	if (kDebugPrint) {
		printf("[HLE3D/V3D] ---- copy screen, to frame %d, from %08x ----\n", params->backBufferIndex, srcAddr);
	}

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

static void FlipBuffers(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, uint32_t pc)
{
	if (backend->isVRally3 || backend->isStuntman) {
		// Since Stuntman and V-Rally 3 move the buffer flip around,
		// we need to verify we're not in the wrong place.
		// Maybe in future this could be a watchpoint rather than a code breakpoint
		uint32_t const opcode = cpu->memory.load32(cpu, pc, NULL);
		if (opcode != 0xe1c020b0) {
			return;
		}
	}

	uint16_t const value = cpu->gprs[2];
	uint8_t const mode = (value & 0x7);
	if (kDebugPrint) {
		printf("[HLE3D/V3D] ---- flip buffers, mode %d frontbuffer %d (writing %04x to %08x) ----\n", mode, (value >> 4) & 1, value, cpu->gprs[0]);
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

static void FillColoredTrapezoid(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params)
{
	backend->b.h->bgMode4active[params->backBufferIndex] = true;
	uint8_t* renderTargetPal = backend->b.h->bgMode4pal[params->backBufferIndex];

	uint8_t const color = cpu->gprs[11];

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



static void DrawVRally3ScaledEnvSprite(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params)
{
	// scaled 8bit sprite for environment details

	// TODO: merge this with Stuntman/Asterix Env Sprite
	// pull all the values from gprs rather than memory
	// since the layout will be consistent
	// just move the breakpoint ahead to after the ldmia?

	backend->b.h->bgMode4active[params->backBufferIndex] = true;
	uint8_t* renderTargetPal = backend->b.h->bgMode4pal[params->backBufferIndex];

	uint32_t const r1 = cpu->gprs[1] + 4;

	uint32_t const texPtr = cpu->memory.load32(cpu, r1+0,  NULL); // r11
	int32_t  const y0     = cpu->memory.load32(cpu, r1+4,  NULL); // r0
	int32_t  const x0     = cpu->memory.load32(cpu, r1+8,  NULL); // r3
	int32_t  const v0     = cpu->memory.load32(cpu, r1+12, NULL); // r4
	int32_t  const u0     = cpu->memory.load32(cpu, r1+16, NULL); // r7
	int32_t  const y1     = cpu->memory.load32(cpu, r1+20, NULL); // r8
	int32_t  const x1     = cpu->memory.load32(cpu, r1+24, NULL); // r9
	int32_t  const v1     = cpu->memory.load32(cpu, r1+28, NULL); // r12
	int32_t  const u1     = cpu->memory.load32(cpu, r1+32, NULL); // lr

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

static void DrawVRally3VehicleInterior(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params)
{
	bool const isOpaque = (cpu->gprs[5] == 1);
	int32_t left = cpu->gprs[6];
	int32_t top = cpu->gprs[7];
	int32_t const texStride = cpu->gprs[8];
	int32_t height = cpu->gprs[9];
	int32_t texPtr = cpu->gprs[10];

	left += cpu->memory.load16(cpu, 0x02039278, NULL);
	top += cpu->memory.load16(cpu, 0x0203927a, NULL);

	int32_t width = texStride;

	if (left < 0) {
		width = left + texStride;
		if (width <= 0)
			return;
		texPtr += -left;
		left = 0;
	}

	if (top < 0) {
		height += top;
		if (height <= 0)
			return;
		texPtr += (-top * texStride);
		top = 0;
	}

	int32_t const rightEdge = (left + width);
	int32_t const rightEdgeOverlap = rightEdge - 240;
	if (rightEdgeOverlap > 0) {
		width -= rightEdgeOverlap;
		if (width <= 0)
			return;
	}

	int32_t const bottomEdge = (top + height);
	int32_t const bottomEdgeOverlap = bottomEdge - 160;
	if (bottomEdgeOverlap > 0) {
		height -= bottomEdgeOverlap;
		if (height <= 0)
			return;
	}

	// vram dest bank
	// cpu->memory.load32(cpu, 0x03003a84, NULL);

	if (kDebugDraw) {
		HLE3DDebugDrawRect(backend->b.h, left, top, width, height, 0x00ffff);
	}

	uint8_t* renderTargetPal = backend->b.h->bgMode4pal[params->backBufferIndex];

	if (isOpaque) {
		for (int gy=0;gy<height;++gy) {
			for (int sy=0;sy<params->scale;++sy) {
				int py = (top+gy)*params->scale+sy;
				for (int gx=0;gx<width;++gx) {
					uint8_t const pixel = cpu->memory.load8(cpu, texPtr+gy*texStride+gx, NULL);
					for (int sx=0;sx<params->scale;++sx) {
						int px = (left+gx)*params->scale+sx;
						renderTargetPal[py*params->rtWidth+px] = pixel;
					}
				}
			}
		}
	} else {
		for (int gy=0;gy<height;++gy) {
			for (int sy=0;sy<params->scale;++sy) {
				int py = (top+gy)*params->scale+sy;
				for (int gx=0;gx<width;++gx) {
					uint8_t const pixel = cpu->memory.load8(cpu, texPtr+gy*texStride+gx, NULL);
					if (pixel != 0) {
						for (int sx=0;sx<params->scale;++sx) {
							int px = (left+gx)*params->scale+sx;
							renderTargetPal[py*params->rtWidth+px] = pixel;
						}
					}
				}
			}
		}
	}
}

static void DrawVRally3VehicleSprite(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params)
{
	uint8_t* renderTargetPal = backend->b.h->bgMode4pal[params->backBufferIndex];

	int32_t const r1 = cpu->gprs[1];
	int32_t const r2 = cpu->gprs[2];

	int32_t const u0 = (r2 >> 16);
	int32_t const v0 = (r1 >> 16);
	int32_t const udelta = (r2 & 0xffff);
	int32_t const vdelta = (r1 & 0xffff);

	int32_t const height = cpu->gprs[5];
	int32_t const width = cpu->gprs[6];
	uint32_t const paletteMask = cpu->gprs[7]; // i think
	uint32_t const spritePtr = cpu->gprs[11];
	uint8_t const spriteStride = cpu->memory.load8(cpu, backend->addrVRally3VehicleSpriteStride, NULL);

	uint32_t const r10 = cpu->gprs[10]; // destination vram pointer
	int const pixelIndex = ((r10-0x06000000)%0xa000);
	int const top = pixelIndex / GBA_VIDEO_HORIZONTAL_PIXELS;
	int const left = pixelIndex % GBA_VIDEO_HORIZONTAL_PIXELS;

	if (kDebugDraw) {
		HLE3DDebugDrawRect(backend->b.h, left, top, width, height, 0xffffff);
	}

	int32_t const scaledLeft   = left * params->scale;
	int32_t const scaledTop    = top  * params->scale;
	int32_t const scaledRight  = scaledLeft + (width  * params->scale);
	int32_t const scaledBottom = scaledTop  + (height * params->scale);

	for (int y = scaledTop; y < scaledBottom; ++y) {
		int32_t const v = (v0 + (vdelta*(y-scaledTop))/params->scale) >> 8;
		for (int x = scaledLeft; x < scaledRight; ++x) {
			int32_t const u = (u0 + (udelta*(x-scaledLeft))/params->scale) >> 8;
			uint8_t const idx = (cpu->memory.load8(cpu, spritePtr + (v*spriteStride)+(u/2), NULL) >> ((u%2)?0:4)) & 0xf;
			if (idx != 0) {
				renderTargetPal[y*params->rtWidth+x] = idx | paletteMask;
			}
		}
	}
}

static void DrawVRally3Text(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params)
{
	uint8_t* renderTargetPal = backend->b.h->bgMode4pal[params->backBufferIndex];

	uint32_t stringStreamPtr = cpu->gprs[0];
	uint32_t const glyphPixelsPtr = cpu->gprs[1];
	uint32_t const glyphInfoTable = cpu->gprs[2];
	uint32_t const glyphStride = cpu->gprs[3];
	uint32_t const height = cpu->gprs[4];
	//uint32_t r5 = cpu->gprs[5]; // destination vram bank

	// loop over strings
	while (true) {
		uint8_t left = cpu->memory.load8(cpu, stringStreamPtr++, NULL);
		if (left == 0xff) {
			break;
		}

		uint8_t const top = cpu->memory.load8(cpu, stringStreamPtr++, NULL);

		// loop over glyphs in the string
		while (true) {
			uint8_t const glyphId = cpu->memory.load8(cpu, stringStreamPtr++, NULL);
			if (glyphId == 0x00) {
				break;
			}

			uint32_t const glyphInfoPtr = glyphInfoTable + (glyphId*4);

			uint16_t const glyphPixelsOffset = cpu->memory.load16(cpu, glyphInfoPtr+0, NULL); // id?
			uint16_t const width = cpu->memory.load16(cpu, glyphInfoPtr+2, NULL); // width?

			if (kDebugDraw) {
				HLE3DDebugDrawRect(backend->b.h, left, top, width, height, 0xff00ff);
			}

			for (uint32_t gy = 0; gy < height; ++gy) {
				for (uint32_t gx = 0; gx < width; ++gx) {
					uint8_t const pixel = cpu->memory.load8(cpu, glyphPixelsPtr+glyphPixelsOffset+(glyphStride*gy)+gx, NULL);
					if (pixel != 0) {
						for (int sy = 0; sy < params->scale; ++sy) {
							int py = (top+gy)*params->scale+sy;
							for (int sx = 0; sx < params->scale; ++sx) {
								int px = (left+gx)*params->scale+sx;
								renderTargetPal[py*params->rtWidth+px] = pixel;
							}
						}
					}
				}
			}
			left += width;
		}
	}
}



static void DrawStuntmanSprite0(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params)
{
	DrawAsterixScaledEnvSprite(backend, cpu, params);
}

static void DrawStuntmanSprite1(struct HLE3DBackendV3D* backend, struct ARMCore* cpu, struct RenderParams const* params)
{
	// TODO: rewrite this based on the assembly?
	// there's an off-by-one error on mirrored sprites in Stuntman?
	// and some off-by-one errors in sizes too?
	DrawAsterixScaledNpcSprite(backend, cpu, params);
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
	int32_t  const v0     = cpu->memory.load32(cpu, r11+12, NULL); // r4
	int32_t  const u0     = cpu->memory.load32(cpu, r11+16, NULL); // r7
	int32_t  const y1     = cpu->memory.load32(cpu, r11+20, NULL); // r8
	int32_t  const x1     = cpu->memory.load32(cpu, r11+24, NULL); // r9
	int32_t  const v1     = cpu->memory.load32(cpu, r11+28, NULL); // r12
	int32_t  const u1     = cpu->memory.load32(cpu, r11+32, NULL); // lr

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
	int32_t  const v0           = cpu->memory.load32(cpu, r11+20, NULL); // r5
	int32_t  const u0           = cpu->memory.load32(cpu, r11+24, NULL); // r6
	int32_t  const y1           = cpu->memory.load32(cpu, r11+28, NULL); // r7
	int32_t  const x1           = cpu->memory.load32(cpu, r11+32, NULL); // r8
	int32_t  const v1           = cpu->memory.load32(cpu, r11+36, NULL); // r9
	int32_t  const u1           = cpu->memory.load32(cpu, r11+40, NULL); // r10

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
