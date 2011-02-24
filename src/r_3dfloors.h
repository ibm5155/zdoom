#ifndef SOFT_FAKE3D_H
#define SOFT_FAKE3D_H

#include "p_3dfloors.h"

// special types

struct HeightLevel
{
	fixed_t height;
	struct HeightLevel *prev;
	struct HeightLevel *next;
};

struct HeightStack
{
	HeightLevel *height_top;
	HeightLevel *height_cur;
	int height_max;
};

struct ClipStack
{
	short floorclip[MAXWIDTH];
	short ceilingclip[MAXWIDTH];
	F3DFloor *ffloor;
	ClipStack *next;
};

// external varialbes

// fake3D flags:
enum
{
	// BSP stage:
	FAKE3D_FAKEFLOOR		= 1,	// fake floor, mark seg as FAKE
	FAKE3D_FAKECEILING		= 2,	// fake ceiling, mark seg as FAKE
	FAKE3D_FAKEBACK			= 4,	// R_AddLine with fake backsector, mark seg as FAKE
	FAKE3D_FAKEMASK			= 7,

	// sorting stage:
	FAKE3D_CLIPBOTTOM		= 1,	// clip bottom
	FAKE3D_CLIPTOP			= 2,	// clip top
	FAKE3D_REFRESHCLIP		= 4,	// refresh clip info
	FAKE3D_DOWN2UP			= 8,	// rendering from down to up (floors)
	FAKE3D_16				= 16,	// what is this?
};

enum EMarkPlaneEdge
{
	MARK_NEAR,
	MARK_FAR
};

extern int fake3D;
extern F3DFloor *fakeFloor;
extern fixed_t fakeHeight;
extern fixed_t fakeAlpha;
extern int fakeActive;
extern fixed_t sclipBottom;
extern fixed_t sclipTop;
extern HeightLevel *height_top;
extern HeightLevel *height_cur;
extern int CurrentMirror;
extern int CurrentSkybox;

// functions
void R_3D_DeleteHeights();
void R_3D_AddHeight(secplane_t *add, sector_t *sec);
void R_3D_NewClip();
void R_3D_ResetClip();
void R_3D_EnterSkybox();
void R_3D_LeaveSkybox();

vissubsector_t *R_3D_EnterSubsector(subsector_t *sub);
void R_3D_MarkPlanes(vissubsector_t *vsub, const FWallTexMapParm *tmap, seg_t *seg, vertex_t *v1, vertex_t *v2);

#endif