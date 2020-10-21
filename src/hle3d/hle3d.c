#include <mgba/internal/arm/arm.h>
#include <mgba/internal/arm/isa-inlines.h>
#include <mgba-util/vfs.h>
#include <mgba/hle3d/hle3d.h>

void HLE3DCreate(struct HLE3D* hle3d)
{
	hle3d->breakpoints = NULL;
	hle3d->activeBackend = NULL;
	HLE3DBackendV3DCreate(&hle3d->backendV3D);
	HLE3DBackendDromeCreate(&hle3d->backendDromeRacers);

	hle3d->renderScale = 0;

	for(int i=0;i<2;++i) {
		hle3d->bgMode4active[i] = false;
		hle3d->bgMode4pal[i] = NULL;
		hle3d->bgMode4color[i] = NULL;
	}

	hle3d->debugRects = NULL;
}

void HLE3DDestroy(struct HLE3D* hle3d)
{
	HLE3DOnUnloadROM(hle3d);
	HLE3DClearBreakpoints(hle3d);
	HLE3DDebugClear(hle3d);
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

	int const scale = hle3d->renderScale;
	for(int i=0;i<2;++i) {
		hle3d->bgMode4active[i] = false;
		memset(hle3d->bgMode4pal[i], 0, 240*160*scale*scale);
		memset(hle3d->bgMode4color[i], 0, 240*160*scale*scale*4);
	}

	if (hle3d->backendV3D.b.isGame(ident)) {
		hle3d->activeBackend = &hle3d->backendV3D.b;
	} else if (hle3d->backendDromeRacers.b.isGame(ident)) {
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
	HLE3DDebugClear(hle3d);

	int const scale = hle3d->renderScale;
	for(int i=0;i<2;++i) {
		hle3d->bgMode4active[i] = false;
		memset(hle3d->bgMode4pal[i], 0, 240*160*scale*scale);
		memset(hle3d->bgMode4color[i], 0, 240*160*scale*scale*4);
	}

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

void HLE3DCommitMode4Buffer(struct HLE3D* hle3d, struct ARMCore* cpu, uint8_t frame)
{
	frame = frame?1:0;

	int const scale = hle3d->renderScale;
	uint8_t* renderTargetPal = hle3d->bgMode4pal[frame];
	uint8_t* renderTargetColor = hle3d->bgMode4color[frame];

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


	struct HLE3DDebugRect* rect = hle3d->debugRects;
	while (rect) {
		int left = rect->x * scale;
		int right = (rect->x + rect->w) * scale;
		int top = rect->y * scale;
		int bottom = (rect->y + rect->h) * scale;

		if (right < 0 || left >= 240*scale || bottom < 0 || top >= 160*scale) {
			rect = rect->next;
			continue;
		}

		if (left < 0)
			left = 0;
		if (right >= 240*scale)
			right = (240*scale)-1;
		if (top < 0)
			top = 0;
		if (bottom >= 160*scale)
			bottom = (160*scale)-1;

		int const stride = 240*scale;
		uint8_t const r = (rect->rgb>>16) & 0xff;
		uint8_t const g = (rect->rgb>>8) & 0xff;
		uint8_t const b = rect->rgb & 0xff;
		for (int x=left;x<=right;++x) {
			renderTargetColor[(top*stride+x)*4+0] = r;
			renderTargetColor[(top*stride+x)*4+1] = g;
			renderTargetColor[(top*stride+x)*4+2] = b;
			renderTargetColor[(top*stride+x)*4+3] = 0xff;
			renderTargetColor[(bottom*stride+x)*4+0] = r;
			renderTargetColor[(bottom*stride+x)*4+1] = g;
			renderTargetColor[(bottom*stride+x)*4+2] = b;
			renderTargetColor[(bottom*stride+x)*4+3] = 0xff;
		}
		for (int y=top;y<=bottom;++y) {
			renderTargetColor[(y*stride+left)*4+0] = r;
			renderTargetColor[(y*stride+left)*4+1] = g;
			renderTargetColor[(y*stride+left)*4+2] = b;
			renderTargetColor[(y*stride+left)*4+3] = 0xff;
			renderTargetColor[(y*stride+right)*4+0] = r;
			renderTargetColor[(y*stride+right)*4+1] = g;
			renderTargetColor[(y*stride+right)*4+2] = b;
			renderTargetColor[(y*stride+right)*4+3] = 0xff;
		}
		rect = rect->next;
	}

	HLE3DDebugClear(hle3d);
}

void HLE3DDebugDrawRect(struct HLE3D* hle3d, int16_t x, int16_t y, uint16_t w, uint16_t h, uint32_t color)
{
	struct HLE3DDebugRect* node = malloc(sizeof(struct HLE3DDebugRect));
	node->x = x;
	node->y = y;
	node->w = w;
	node->h = h;
	node->rgb = color;
	node->next = NULL;

	if (hle3d->debugRects == NULL) {
		hle3d->debugRects = node;
	} else {
		struct HLE3DDebugRect* tail = hle3d->debugRects;
		while (tail->next != NULL) {
			tail = tail->next;
		}
		tail->next = node;
	}
}

void HLE3DDebugClear(struct HLE3D* hle3d)
{
	struct HLE3DDebugRect* rect = hle3d->debugRects;
	while (rect) {
		struct HLE3DDebugRect* next = rect->next;
		free(rect);
		rect = next;
	}
	hle3d->debugRects = NULL;
}
