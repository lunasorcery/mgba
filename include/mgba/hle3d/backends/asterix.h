#pragma once
#include <mgba/internal/arm/arm.h>
#include <mgba/hle3d/backend.h>

struct HLE3DBackendAsterix {
	struct HLE3DBackend b;

	uint32_t addrFuncClearScreen;
	uint32_t addrFuncCopyScreen;
	uint32_t addrScreenCopySource;
	uint32_t addrFuncFlipBuffers;
	uint32_t addrActiveFrame;

	uint32_t addrFuncColoredTrapezoid;
	uint32_t addrColoredPolyColor;

	uint32_t addrFuncTexture1pxTrapezoid;
	uint32_t addrTex1UvRowDelta0;
	uint32_t addrTex1UvRowDelta1;
	uint32_t addrTex1Uv0;
	uint32_t addrTex1Uv1;

	uint32_t addrFuncTexture2pxTrapezoid;
	uint32_t addrTex2UvRowDelta0;
	uint32_t addrTex2UvRowDelta1;
	uint32_t addrTex2Uv0;
	uint32_t addrTex2Uv1;

	bool isDriv3r;
	uint32_t addrFuncDriv3rPlayerSprite;
	uint32_t addrFuncDriv3rScaledSprite;

	bool isAsterix;
	uint32_t addrFuncAsterixPlayerSprite0;
	uint32_t addrFuncAsterixPlayerSprite1;
	uint32_t addrFuncAsterixScaledEnvSprite;
	uint32_t addrFuncAsterixScaledNpcSprite;
	uint32_t addrFuncAsterixMenuOverlay;
	uint32_t addrFuncAsterixScreenCopyHorizontalScroll;
	uint32_t addrFuncAsterixScreenCopyVerticalScroll;
};

void HLE3DBackendAsterixCreate(struct HLE3DBackendAsterix* backend);

void HLE3DBackendAsterixInit(struct HLE3DBackend* backend, struct HLE3D* hle3d, uint32_t ident);
void HLE3DBackendAsterixDeinit(struct HLE3DBackend* backend);
bool HLE3DBackendAsterixIsGame(uint32_t ident);
void HLE3DBackendAsterixHook(struct HLE3DBackend* backend, struct ARMCore* cpu, uint32_t pc);
