#pragma once
#include <mgba/internal/arm/arm.h>
#include <mgba/internal/gba/gba.h>

struct HLE3DBackend {
	struct HLE3D* h;
	void (*init)(struct HLE3DBackend*, struct HLE3D*, uint32_t);
	void (*deinit)(struct HLE3DBackend*);
	bool (*isGame)(uint32_t);
	void (*hook)(struct HLE3DBackend*, struct ARMCore*, uint32_t);
};
