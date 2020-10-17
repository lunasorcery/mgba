#include <mgba/internal/arm/arm.h>
#include <mgba/internal/arm/isa-inlines.h>
#include <mgba/hle3d/hle3d.h>
#include <mgba/hle3d/backends/drome.h>
#include <mgba-util/vfs.h>

void HLE3DCreate(struct HLE3D* hle3d)
{
	hle3d->breakpoints = NULL;
	hle3d->activeBackend = NULL;
	HLE3DBackendDromeCreate(&hle3d->backendDromeRacers);

	hle3d->renderScale = 0;

	for(int i=0;i<2;++i) {
		hle3d->bgMode4active[i] = false;
		hle3d->bgMode4pal[i] = NULL;
		hle3d->bgMode4color[i] = NULL;
	}
}

void HLE3DDestroy(struct HLE3D* hle3d)
{
	HLE3DOnUnloadROM(hle3d);
	HLE3DClearBreakpoints(hle3d);
	for(int i=0;i<2;++i) {
		free(hle3d->bgMode4pal[i]);
		free(hle3d->bgMode4color[i]);
	}
}

void HLE3DSetRenderScale(struct HLE3D* hle3d, int scale)
{
	if (hle3d->renderScale == scale)
		return;

	hle3d->renderScale = scale;
	for(int i=0;i<2;++i) {
		free(hle3d->bgMode4pal[i]);
		free(hle3d->bgMode4color[i]);
	}
	for(int i=0;i<2;++i) {
		hle3d->bgMode4active[i] = false;
		hle3d->bgMode4pal[i] = malloc(240*160*scale*scale);
		hle3d->bgMode4color[i] = malloc(240*160*scale*scale*4);
	}
}

void HLE3DOnLoadROM(struct HLE3D* hle3d, struct VFile* vf)
{
	HLE3DOnUnloadROM(hle3d);

	if (!vf) {
		return;
	}

	uint8_t identChars[4];
	vf->seek(vf, 0xAC, SEEK_SET);
	vf->read(vf, &identChars, 4);

	uint32_t ident = identChars[0];
	ident |= identChars[1] << 8;
	ident |= identChars[2] << 16;
	ident |= identChars[3] << 24;

	if (hle3d->backendDromeRacers.b.isGame(ident)) {
		hle3d->activeBackend = &hle3d->backendDromeRacers.b;
	}

	if (hle3d->activeBackend) {
		hle3d->activeBackend->h = hle3d;
		hle3d->activeBackend->init(hle3d->activeBackend, hle3d, ident);
	}
}

void HLE3DOnUnloadROM(struct HLE3D* hle3d)
{
	HLE3DClearBreakpoints(hle3d);

	hle3d->bgMode4active[0] = false;
	hle3d->bgMode4active[1] = false;

	if (hle3d->activeBackend) {
		hle3d->activeBackend->deinit(hle3d->activeBackend);
		hle3d->activeBackend = NULL;
	}
}

void HLE3DHook(struct HLE3D* hle3d, struct ARMCore* cpu, uint32_t pc)
{
	if (hle3d->activeBackend) {
		hle3d->activeBackend->hook(hle3d->activeBackend, cpu, pc);
	}
}

void HLE3DAddBreakpoint(struct HLE3D* hle3d, uint32_t address)
{
	// skip if it's already here
	struct HLE3DBreakpoint* bp = hle3d->breakpoints;
	while (bp) {
		if (bp->address == address) {
			return;
		}
		bp = bp->next;
	}

	// push front
	struct HLE3DBreakpoint* head = hle3d->breakpoints;
	struct HLE3DBreakpoint* node = malloc(sizeof(struct HLE3DBreakpoint));
	node->address = address;
	node->next = head;
	hle3d->breakpoints = node;
}

void HLE3DClearBreakpoints(struct HLE3D* hle3d)
{
	struct HLE3DBreakpoint* bp = hle3d->breakpoints;
	while (bp) {
		struct HLE3DBreakpoint* next = bp->next;
		free(bp);
		bp = next;
	}
	hle3d->breakpoints = NULL;
}

void HLE3DCheckBreakpoints(struct HLE3D* hle3d, struct ARMCore* cpu)
{
	int const instructionLength = _ARMInstructionLength(cpu);
	uint32_t const pc = cpu->gprs[ARM_PC] - instructionLength;

	struct HLE3DBreakpoint* bp = hle3d->breakpoints;
	while (bp) {
		if (bp->address == pc) {
			HLE3DHook(hle3d, cpu, pc);
			break;
		}
		bp = bp->next;
	}
}
