/*
** edata.cpp
** Parses Eternity Extradata lumps
**
**---------------------------------------------------------------------------
** Copyright 2015 Christoph Oelckers
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
** This code was written based on the documentation in the Eternity Wiki
**
*/

#include "w_wad.h"
#include "m_argv.h"
#include "zstring.h"
#include "sc_man.h"
#include "g_level.h"
#include "doomdata.h"
#include "r_defs.h"
#include "info.h"
#include "p_lnspec.h"
#include "p_setup.h"
#include "p_tags.h"
#include "p_terrain.h"
#include "v_palette.h"
#include "p_acs.h"
#include "r_data/colormaps.h"


struct FEDOptions : public FOptionalMapinfoData
{
	FEDOptions()
	{
		identifier = "EData";
	}
	virtual FOptionalMapinfoData *Clone() const
	{
		FEDOptions *newopt = new FEDOptions;
		newopt->identifier = identifier;
		newopt->EDName = EDName;
		newopt->acsName = acsName;
		return newopt;
	}
	FString EDName;
	FString acsName;
};

DEFINE_MAP_OPTION(edata, false)
{
	FEDOptions *opt = info->GetOptData<FEDOptions>("EData");

	parse.ParseAssign();
	parse.sc.MustGetString();
	opt->EDName = parse.sc.String;
}

DEFINE_MAP_OPTION(loadacs, false)
{
	FEDOptions *opt = info->GetOptData<FEDOptions>("EData");

	parse.ParseAssign();
	parse.sc.MustGetString();
	opt->acsName = parse.sc.String;
}

struct EDMapthing
{
	int recordnum;
	int tid;
	int type;
	fixed_t height;
	int args[5];
	WORD skillfilter;
	DWORD flags;
};

struct EDLinedef
{
	int recordnum;
	int special;
	int tag;
	int id;
	int args[5];
	fixed_t alpha;
	DWORD flags;
	DWORD activation;
};



struct EDSector
{
	int recordnum;

	DWORD flags;
	DWORD flagsRemove;
	DWORD flagsAdd;

	int damageamount;
	int damageinterval;
	FNameNoInit damagetype;
	BYTE leaky;
	BYTE leakyadd;
	BYTE leakyremove;
	int floorterrain;
	int ceilingterrain;

	DWORD color;

	DWORD damageflags;
	DWORD damageflagsAdd;
	DWORD damageflagsRemove;

	bool flagsSet;
	bool damageflagsSet;
	bool colorSet;

	// colormaptop//bottom cannot be used because ZDoom has no corresponding properties.

	FTransform planexform[2];
	DWORD portalflags[2];
	fixed_t overlayalpha[2];
};

static FString EDMap;
static TMap<int, EDLinedef> EDLines;
static TMap<int, EDSector> EDSectors;
static TMap<int, EDMapthing> EDThings;


static void parseLinedef(FScanner &sc)
{
	EDLinedef ld;
	bool argsset = false;

	memset(&ld, 0, sizeof(ld));
	ld.alpha = FRACUNIT;

	sc.MustGetStringName("{");
	while (!sc.CheckString("}"))
	{
		sc.MustGetString();
		if (sc.Compare("recordnum"))
		{
			sc.CheckString("=");
			sc.MustGetNumber();
			ld.recordnum = sc.Number;
		}
		else if (sc.Compare("tag"))
		{
			sc.CheckString("=");
			sc.MustGetNumber();
			ld.tag = sc.Number;
		}
		else if (sc.Compare("id"))
		{
			sc.CheckString("=");
			sc.MustGetNumber();
			ld.id = sc.Number;
		}
		else if (sc.Compare("special"))
		{
			sc.CheckString("=");
			if (sc.CheckNumber())
			{
				// Oh joy, this is going to be fun...
				// Here we cannot do anything because we need the tag to make this work. 
				// For now just store a negative number and resolve this later.
				ld.special = -sc.Number;
			}
			else
			{
				sc.MustGetString();
				ld.special = P_FindLineSpecial(sc.String);
			}
		}
		else if (sc.Compare("args"))
		{
			sc.CheckString("=");
			sc.MustGetStringName("{");
			int c = 0;
			while (true)
			{
				sc.MustGetNumber();
				ld.args[c++] = sc.Number;
				if (sc.CheckString("}")) break;
				sc.MustGetStringName(",");
			}
			argsset = true;
		}
		else if (sc.Compare("alpha"))
		{
			sc.CheckString("=");
			sc.MustGetFloat();
			ld.alpha = FLOAT2FIXED(sc.Float);
		}
		else if (sc.Compare("extflags"))
		{
			// these are needed to build the proper activation mask out of the possible flags which do not match ZDoom 1:1.
			DWORD actmethod = 0;
			DWORD acttype = 0;
			do
			{
				sc.CheckString("=");
				sc.MustGetString();
				for (const char *tok = strtok(sc.String, ",+ \t"); tok != NULL; tok = strtok(NULL, ",+ \t"))
				{
					if (!stricmp(tok, "USE")) actmethod |= SPAC_Use | SPAC_MUse;
					else if (!stricmp(tok, "CROSS")) actmethod |= SPAC_Cross | SPAC_MCross | SPAC_PCross;
					else if (!stricmp(tok, "IMPACT")) ld.activation |= SPAC_Impact;
					else if (!stricmp(tok, "PUSH")) actmethod |= SPAC_Push;
					else if (!stricmp(tok, "PLAYER")) acttype |= SPAC_Use | SPAC_Cross | SPAC_Push;
					else if (!stricmp(tok, "MONSTER")) acttype |= SPAC_MUse | SPAC_MCross | SPAC_MPush;
					else if (!stricmp(tok, "MISSILE")) acttype |= SPAC_PCross;
					else if (!stricmp(tok, "REPEAT")) ld.flags |= ML_REPEAT_SPECIAL;
					else if (!stricmp(tok, "1SONLY")) ld.flags |= ML_FIRSTSIDEONLY;
					else if (!stricmp(tok, "ADDITIVE")) ld.flags |= ML_ADDTRANS;
					else if (!stricmp(tok, "BLOCKALL")) ld.flags |= ML_BLOCKEVERYTHING;
					else if (!stricmp(tok, "ZONEBOUNDARY")) ld.flags |= ML_ZONEBOUNDARY;
					else if (!stricmp(tok, "CLIPMIDTEX")) ld.flags |= ML_CLIP_MIDTEX;
					else sc.ScriptError("Unknown option '%s'", tok);
				}
			} while (sc.CheckString("|"));	// Unquoted strings with '|' separator - parse as a separate string in the loop.

			// and finally we must mask in the activation method
			ld.activation |= (actmethod & acttype);
		}
		else
		{
			sc.ScriptError("Unknown property '%s'", sc.String);
		}
	}
	if (ld.tag == 0) ld.tag = ld.id;	// urgh...
	if (ld.special < 0)	// translate numeric specials.
	{
		line_t line;
		maplinedef_t mld;
		mld.special = -ld.special;
		mld.tag = ld.tag;
		P_TranslateLineDef(&line, &mld);
		ld.special = line.special;
		ld.activation = line.activation;
		ld.flags = (ld.flags & ~(ML_REPEAT_SPECIAL | ML_FIRSTSIDEONLY)) | (line.flags & (ML_REPEAT_SPECIAL | ML_FIRSTSIDEONLY));
		if (!argsset) memcpy(ld.args, line.args, sizeof(ld.args));
	}
	 EDLines[ld.recordnum] = ld;
}

static void parseSector(FScanner &sc)
{
	EDSector sec;

	memset(&sec, 0, sizeof(sec));
	sec.overlayalpha[sector_t::floor] = sec.overlayalpha[sector_t::ceiling] = FRACUNIT;
	sec.floorterrain = sec.ceilingterrain = -1;

	sc.MustGetStringName("{");
	while (!sc.CheckString("}"))
	{
		sc.MustGetString();
		if (sc.Compare("recordnum"))
		{
			sc.CheckString("=");
			sc.MustGetNumber();
			sec.recordnum = sc.Number;
		}
		else if (sc.Compare("flags"))
		{
			DWORD *flagvar = NULL;
			if (sc.CheckString("."))
			{
				sc.MustGetString();
				if (sc.Compare("add"))
				{
					flagvar = &sec.flagsAdd;
				}
				else if (sc.Compare("remove"))
				{
					flagvar = &sec.flagsRemove;
				}
				else
				{
					sc.ScriptError("Invalid property  'flags.%s'", sc.String);
				}
			}
			else
			{
				sec.flagsSet = true;
				flagvar = &sec.flags;
			}
			sc.CheckString("=");
			do
			{
				sc.MustGetString();
				for (const char *tok = strtok(sc.String, ",+ \t"); tok != NULL; tok = strtok(NULL, ",+ \t"))
				{
					if (!stricmp(tok, "SECRET")) *flagvar |= SECF_SECRET | SECF_WASSECRET;
					else if (!stricmp(tok, "FRICTION")) *flagvar |= SECF_FRICTION;
					else if (!stricmp(tok, "PUSH")) *flagvar |= SECF_PUSH;
					else if (!stricmp(tok, "KILLSOUND")) *flagvar |= SECF_SILENT;
					else if (!stricmp(tok, "KILLMOVESOUND")) *flagvar |= SECF_SILENTMOVE;
					else sc.ScriptError("Unknown option '%s'", tok);
				}
			} while (sc.CheckString("|"));	// Unquoted strings with '|' separator - parse as a separate string in the loop.
		}
		else if (sc.Compare("damage"))
		{
			sc.CheckString("=");
			sc.MustGetNumber();
			sec.damageamount = sc.Number;
		}
		else if (sc.Compare("damagemask"))
		{
			sc.CheckString("=");
			sc.MustGetNumber();
			sec.damageinterval = sc.Number;
		}
		else if (sc.Compare("damageflags"))
		{
			DWORD *flagvar = NULL;
			BYTE *leakvar = NULL;
			if (sc.CheckString("."))
			{
				sc.MustGetString();
				if (sc.Compare("add"))
				{
					flagvar = &sec.damageflagsAdd;
					leakvar = &sec.leakyadd;
				}
				else if (sc.Compare("remove"))
				{
					flagvar = &sec.damageflagsRemove;
					leakvar = &sec.leakyremove;
				}
				else
				{
					sc.ScriptError("Invalid property  'flags.%s'", sc.String);
				}
			}
			else
			{
				sec.damageflagsSet = true;
				flagvar = &sec.damageflags;
				leakvar = &sec.leaky;
			}
			sc.CheckString("=");
			do
			{
				sc.MustGetString();
				for (const char *tok = strtok(sc.String, ",+ \t"); tok != NULL; tok = strtok(NULL, ",+ \t"))
				{
					if (!stricmp(tok, "LEAKYSUIT")) *leakvar |= 1;
					else if (!stricmp(tok, "IGNORESUIT")) *leakvar |= 2;	// these 2 bits will be used to set 'leakychance', but this can only be done when the sector gets initialized
					else if (!stricmp(tok, "ENDGODMODE")) *flagvar |= SECF_ENDGODMODE;
					else if (!stricmp(tok, "ENDLEVEL")) *flagvar |= SECF_ENDLEVEL;
					else if (!stricmp(tok, "TERRAINHIT")) *flagvar |= SECF_DMGTERRAINFX;
					else sc.ScriptError("Unknown option '%s'", tok);
				}
			} while (sc.CheckString("|"));	// Unquoted strings with '|' separator - parse as a separate string in the loop.
		}
		else if (sc.Compare("floorterrain"))
		{
			sc.CheckString("=");
			sc.MustGetString();
			sec.floorterrain = P_FindTerrain(sc.String);
		}
		else if (sc.Compare("floorangle"))
		{
			sc.CheckString("=");
			sc.MustGetFloat();
			sec.planexform[sector_t::floor].angle = FLOAT2ANGLE(sc.Float);
		}
		else if (sc.Compare("flooroffsetx"))
		{
			sc.CheckString("=");
			sc.MustGetFloat();
			sec.planexform[sector_t::floor].xoffs = FLOAT2FIXED(sc.Float);
		}
		else if (sc.Compare("flooroffsety"))
		{
			sc.CheckString("=");
			sc.MustGetFloat();
			sec.planexform[sector_t::floor].yoffs = FLOAT2FIXED(sc.Float);
		}
		else if (sc.Compare("ceilingterrain"))
		{
			sc.CheckString("=");
			sc.MustGetString();
			sec.ceilingterrain = P_FindTerrain(sc.String);
		}
		else if (sc.Compare("ceilingangle"))
		{
			sc.CheckString("=");
			sc.MustGetFloat();
			sec.planexform[sector_t::ceiling].angle = FLOAT2ANGLE(sc.Float);
		}
		else if (sc.Compare("ceilingoffsetx"))
		{
			sc.CheckString("=");
			sc.MustGetFloat();
			sec.planexform[sector_t::ceiling].xoffs = FLOAT2FIXED(sc.Float);
		}
		else if (sc.Compare("ceilingoffsety"))
		{
			sc.CheckString("=");
			sc.MustGetFloat();
			sec.planexform[sector_t::ceiling].yoffs = FLOAT2FIXED(sc.Float);
		}
		else if (sc.Compare("colormaptop") || sc.Compare("colormapbottom"))
		{
			sc.CheckString("=");
			sc.MustGetString();
			// these properties are not implemented by ZDoom
		}
		else if (sc.Compare("colormapmid"))
		{
			sc.CheckString("=");
			sc.MustGetString();
			// Eternity is based on SMMU and uses colormaps differently than all other ports.
			// The only solution here is to convert the colormap to an RGB value and set it as the sector's color.
			DWORD cmap = R_ColormapNumForName(sc.String);
			if (cmap != 0)
			{
				sec.color = R_BlendForColormap(cmap) & 0xff000000;
				sec.colorSet = true;
			}
		}
		else if (sc.Compare("overlayalpha"))
		{
			sc.MustGetStringName(".");
			sc.MustGetString();
			if (sc.Compare("floor"))
			{
				sc.MustGetNumber();
				if (sc.CheckString("%")) sc.Float = sc.Number / 100.f;
				else sc.Float = sc.Number / 255.f;
				sec.overlayalpha[sector_t::floor] = FLOAT2FIXED(sc.Float);
			}
			else if (sc.Compare("ceiling"))
			{
				sc.MustGetFloat();
				if (sc.CheckString("%")) sc.Float = sc.Number / 100.f;
				else sc.Float = sc.Number / 255.f;
				sec.overlayalpha[sector_t::floor] = FLOAT2FIXED(sc.Float);
			}
		}
		else if (sc.Compare("portalflags"))
		{
			int dest = 0;
			sc.MustGetStringName(".");
			sc.MustGetString();
			if (sc.Compare("floor")) dest = sector_t::floor;
			else if (sc.Compare("ceiling")) dest = sector_t::ceiling;
			else sc.ScriptError("Unknown portal type '%s'", sc.String);

			sc.CheckString("=");
			do
			{
				sc.MustGetString();
				for (const char *tok = strtok(sc.String, ",+ \t"); tok != NULL; tok = strtok(NULL, ",+ \t"))
				{
					if (!stricmp(tok, "DISABLED")) sec.portalflags[dest] |= PLANEF_DISABLED;
					else if (!stricmp(tok, "NORENDER")) sec.portalflags[dest] |= PLANEF_NORENDER;
					else if (!stricmp(tok, "NOPASS")) sec.portalflags[dest] |= PLANEF_NOPASS;
					else if (!stricmp(tok, "BLOCKSOUND")) sec.portalflags[dest] |= PLANEF_BLOCKSOUND;
					else if (!stricmp(tok, "OVERLAY")) sec.portalflags[dest] |= 0;	// we do not use this. Alpha is the sole determinant for overlay drawing
					else if (!stricmp(tok, "ADDITIVE")) sec.portalflags[dest] |= PLANEF_ADDITIVE;
					else if (!stricmp(tok, "USEGLOBALTEX")) {}	// not implemented
					else sc.ScriptError("Unknown option '%s'", tok);
				}
			} while (sc.CheckString("|"));	// Unquoted strings with '|' separator - parse as a separate string in the loop.
		}
		else
		{
			sc.ScriptError("Unknown property '%s'", sc.String);
		}
	}
	EDSectors[sec.recordnum] = sec;
}

static void parseMapthing(FScanner &sc)
{
	EDMapthing mt;

	memset(&mt, 0, sizeof(mt));
	mt.flags |= MTF_SINGLE | MTF_COOPERATIVE | MTF_DEATHMATCH;	// Extradata uses inverse logic, like Doom.exe

	sc.MustGetStringName("{");
	while (!sc.CheckString("}"))
	{
		sc.MustGetString();
		if (sc.Compare("recordnum"))
		{
			sc.CheckString("=");
			sc.MustGetNumber();
			mt.recordnum = sc.Number;
		}
		else if (sc.Compare("tid"))
		{
			sc.CheckString("=");
			sc.MustGetNumber();
			mt.tid = sc.Number;
		}
		else if (sc.Compare("type"))
		{
			sc.CheckString("=");
			if (sc.CheckNumber())
			{
				mt.type = sc.Number;
			}
			else
			{
				// Class name.
				sc.MustGetString();
				// According to the Eternity Wiki a name may be prefixed with 'thing:'.
				const char *pos = strchr(sc.String, ':');	// Eternity never checks if the prefix actually is 'thing'.
				if (pos) pos++;
				else pos = sc.String;
				const PClass *cls = PClass::FindClass(pos);
				if (cls != NULL)
				{
					FDoomEdMap::Iterator it(DoomEdMap);
					FDoomEdMap::Pair *pair;
					while (it.NextPair(pair))
					{
						if (pair->Value.Type == cls)
						{
							mt.type = pair->Key;
							break;
						}
					}
				}
				else
				{
					// Let's hope this isn't an internal Eternity name.
					// If so, a name mapping needs to be defined...
					sc.ScriptError("Unknown type '%s'", sc.String);
				}

			}
		}
		else if (sc.Compare("args"))
		{
			sc.CheckString("=");
			sc.MustGetStringName("{");
			int c = 0;
			while (!sc.CheckString("}"))
			{
				sc.MustGetNumber();
				mt.args[c++] = sc.Number;

			}
		}
		else if (sc.Compare("height"))
		{
			sc.CheckString("=");
			sc.MustGetFloat();	// no idea if Eternity allows fractional numbers. Better be safe and do it anyway.
			mt.height = FLOAT2FIXED(sc.Float);
		}
		else if (sc.Compare("options"))
		{
			sc.CheckString("=");
			do
			{
				sc.MustGetString();
				for (const char *tok = strtok(sc.String, ",+ \t"); tok != NULL; tok = strtok(NULL, ",+ \t"))
				{
					if (!stricmp(tok, "EASY")) mt.skillfilter |= 3;
					else if (!stricmp(tok, "NORMAL")) mt.skillfilter |= 4;
					else if (!stricmp(tok, "HARD")) mt.skillfilter |= 24;
					else if (!stricmp(tok, "AMBUSH")) mt.flags |= MTF_AMBUSH;
					else if (!stricmp(tok, "NOTSINGLE")) mt.flags &= ~MTF_SINGLE;
					else if (!stricmp(tok, "NOTDM")) mt.flags &= ~MTF_DEATHMATCH;
					else if (!stricmp(tok, "NOTCOOP")) mt.flags &= ~MTF_COOPERATIVE;
					else if (!stricmp(tok, "FRIEND")) mt.flags |= MTF_FRIENDLY;
					else if (!stricmp(tok, "DORMANT")) mt.flags |= MTF_DORMANT;
					else sc.ScriptError("Unknown option '%s'", tok);
				}
			} while (sc.CheckString("|"));	// Unquoted strings with '|' separator - parse as a separate string in the loop.
		}
		else
		{
			sc.ScriptError("Unknown property '%s'", sc.String);
		}
	}
	EDThings[mt.recordnum] = mt;
}

void InitED()
{
	FString filename;
	FScanner sc;

	if (EDMap.CompareNoCase(level.MapName) != 0)
	{
		EDLines.Clear();
		EDSectors.Clear();
		EDThings.Clear();
		EDMap = level.MapName;

		const char *arg = Args->CheckValue("-edf");

		if (arg != NULL) filename = arg;
		else
		{
			FEDOptions *opt = level.info->GetOptData<FEDOptions>("EData", false);
			if (opt != NULL)
			{
				filename = opt->EDName;
			}
		}

		if (filename.IsEmpty()) return;
		int lump = Wads.CheckNumForFullName(filename, true, ns_global);
		if (lump == -1) return;
		sc.OpenLumpNum(lump);

		sc.SetCMode(true);
		while (sc.GetString())
		{
			if (sc.Compare("linedef"))
			{
				parseLinedef(sc);
			}
			else if (sc.Compare("mapthing"))
			{
				parseMapthing(sc);
			}
			else if (sc.Compare("sector"))
			{
				parseSector(sc);
			}
			else
			{
				sc.ScriptError("Unknown keyword '%s'", sc.String);
			}
		}
	}
}

void ProcessEDMapthing(FMapThing *mt, int recordnum)
{
	InitED();

	EDMapthing *emt = EDThings.CheckKey(recordnum);
	if (emt == NULL)
	{
		Printf("EDF Mapthing record %d not found\n", recordnum);
		mt->EdNum = 0;
		return;
	}
	mt->thingid = emt->tid;
	mt->EdNum = emt->type;
	mt->info = DoomEdMap.CheckKey(mt->EdNum);
	mt->z = emt->height;
	memcpy(mt->args, emt->args, sizeof(mt->args));
	mt->SkillFilter = emt->skillfilter;
	mt->flags = emt->flags;
}

void ProcessEDLinedef(line_t *ld, int recordnum)
{
	InitED();

	EDLinedef *eld = EDLines.CheckKey(recordnum);
	if (eld == NULL)
	{
		Printf("EDF Linedef record %d not found\n", recordnum);
		ld->special = 0;
		return;
	}
	const DWORD fmask = ML_REPEAT_SPECIAL | ML_FIRSTSIDEONLY | ML_ADDTRANS | ML_BLOCKEVERYTHING | ML_ZONEBOUNDARY | ML_CLIP_MIDTEX;
	ld->special = eld->special;
	ld->activation = eld->activation;
	ld->flags = (ld->flags&~fmask) | eld->flags;
	ld->Alpha = eld->alpha;
	memcpy(ld->args, eld->args, sizeof(ld->args));
	tagManager.AddLineID(int(ld - lines), eld->tag);
}

void ProcessEDSector(sector_t *sec, int recordnum)
{
	EDSector *esec = EDSectors.CheckKey(recordnum);
	if (esec == NULL)
	{
		Printf("EDF Sector record %d not found\n", recordnum);
		return;
	}

	// In ZDoom the regular and the damage flags are part of the same flag word so we need to do some masking.
	const DWORD flagmask = SECF_SECRET | SECF_WASSECRET | SECF_FRICTION | SECF_PUSH | SECF_SILENT | SECF_SILENTMOVE;
	if (esec->flagsSet) sec->Flags = (sec->Flags & ~flagmask);
	sec->Flags = (sec->Flags | esec->flags | esec->flagsAdd) & ~esec->flagsRemove;

	BYTE leak = 0;
	if (esec->damageflagsSet) sec->Flags = (sec->Flags & ~SECF_DAMAGEFLAGS);
	else leak = sec->leakydamage >= 256 ? 2 : sec->leakydamage >= 5 ? 1 : 0;
	sec->Flags = (sec->Flags | esec->damageflags | esec->damageflagsAdd) & ~esec->damageflagsRemove;
	leak = (leak | esec->leaky | esec->leakyadd) & ~esec->leakyremove;

	// the damage properties will be unconditionally overridden by Extradata.
	sec->leakydamage = leak == 0 ? 0 : leak == 1 ? 5 : 256;
	sec->damageamount = esec->damageamount;
	sec->damageinterval = esec->damageinterval;
	sec->damagetype = esec->damagetype;

	sec->terrainnum[sector_t::floor] = esec->floorterrain;
	sec->terrainnum[sector_t::ceiling] = esec->ceilingterrain;

	if (esec->colorSet) sec->SetColor(RPART(esec->color), GPART(esec->color), BPART(esec->color), 0);

	const DWORD pflagmask = PLANEF_DISABLED | PLANEF_NORENDER | PLANEF_NOPASS | PLANEF_BLOCKSOUND | PLANEF_ADDITIVE;
	for (int i = 0; i < 2; i++)
	{
		sec->planes[i].xform.xoffs = esec->planexform[i].xoffs;
		sec->planes[i].xform.yoffs = esec->planexform[i].yoffs;
		sec->planes[i].xform.angle = esec->planexform[i].angle;
		sec->planes[i].alpha = esec->overlayalpha[i];
		sec->planes[i].Flags = (sec->planes[i].Flags & ~pflagmask) | esec->portalflags[i];
	}
}


void ProcessEDSectors()
{
	int i;

	InitED();
	if (EDSectors.CountUsed() == 0) return;	// don't waste time if there's no records.

	// collect all Extradata sector records up front so we do not need to search the complete line array for each sector separately.
	int *sectorrecord = new int[numsectors];
	memset(sectorrecord, -1, numsectors * sizeof(int));
	for (i = 0; i < numlines; i++)
	{
		if (lines[i].special == Static_Init && lines[i].args[1] == Init_EDSector)
		{
			sectorrecord[lines[i].frontsector - sectors] = lines[i].args[0];
			lines[i].special = 0;
		}
	}
	for (i = 0; i < numsectors; i++)
	{
		if (sectorrecord[i] >= 0)
		{
			ProcessEDSector(&sectors[i], sectorrecord[i]);
		}
	}
	delete[] sectorrecord;
}

void LoadMapinfoACSLump()
{
	FEDOptions *opt = level.info->GetOptData<FEDOptions>("EData", false);
	if (opt != NULL)
	{
		int lump = Wads.CheckNumForName(opt->acsName);
		if (lump >= 0) FBehavior::StaticLoadModule(lump);
	}
}
