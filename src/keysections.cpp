/*
** keysections.cpp
** Custom key bindings
**
**---------------------------------------------------------------------------
** Copyright 1998-2009 Randy Heit
** Copyright 2010 Christoph Oelckers
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
*/


#include "menu/menu.h"
#include "g_level.h"
#include "d_player.h"
#include "gi.h"
#include "c_bind.h"
#include "c_dispatch.h"
#include "gameconfigfile.h"

TArray<FKeySection> KeySections;

static void LoadKeys (const char *modname, bool dbl)
{
	char section[64];

	mysnprintf (section, countof(section), "%s.%s%sBindings", gameinfo.ConfigName.GetChars(), modname,
		dbl ? ".Double" : ".");

	FKeyBindings *bindings = dbl? &DoubleBindings : &Bindings;
	if (GameConfig->SetSection (section))
	{
		const char *key, *value;
		while (GameConfig->NextInSection (key, value))
		{
			bindings->DoBind (key, value);
		}
	}
}

static void DoSaveKeys (FConfigFile *config, const char *section, FKeySection *keysection, bool dbl)
{
	config->SetSection (section, true);
	config->ClearCurrentSection ();
	FKeyBindings *bindings = dbl? &DoubleBindings : &Bindings;
	for (unsigned i = 0; i < keysection->mActions.Size(); ++i)
	{
		bindings->ArchiveBindings (config, keysection->mActions[i].mAction);
	}
}

void M_SaveCustomKeys (FConfigFile *config, char *section, char *subsection, size_t sublen)
{
	for (unsigned i=0; i<KeySections.Size(); i++)
	{
		mysnprintf (subsection, sublen, "%s.Bindings", KeySections[i].mSection.GetChars());
		DoSaveKeys (config, section, &KeySections[i], false);
		mysnprintf (subsection, sublen, "%s.DoubleBindings", KeySections[i].mSection.GetChars());
		DoSaveKeys (config, section, &KeySections[i], true);
	}
}

static int CurrentKeySection = -1;

CCMD (addkeysection)
{
	if (ParsingKeyConf)
	{
		if (argv.argc() != 3)
		{
			Printf ("Usage: addkeysection <menu section name> <ini name>\n");
			return;
		}

		// Limit the ini name to 32 chars
		if (strlen (argv[2]) > 32)
			argv[2][32] = 0;

		for (unsigned i = 0; i < KeySections.Size(); i++)
		{
			if (KeySections[i].mTitle.CompareNoCase(argv[2]) == 0)
			{
				CurrentKeySection = i;
				return;
			}
		}

		CurrentKeySection = KeySections.Reserve(1);
		KeySections[CurrentKeySection].mTitle = argv[1];
		KeySections[CurrentKeySection].mSection = argv[2];
		// Load bindings for this section from the ini
		LoadKeys (argv[2], 0);
		LoadKeys (argv[2], 1);
	}
}

CCMD (addmenukey)
{
	if (ParsingKeyConf)
	{
		if (argv.argc() != 3)
		{
			Printf ("Usage: addmenukey <description> <command>\n");
			return;
		}
		if (CurrentKeySection == -1 || CurrentKeySection >= (int)KeySections.Size())
		{
			Printf ("You must use addkeysection first.\n");
			return;
		}

		FKeySection *sect = &KeySections[CurrentKeySection];

		FKeyAction *act = &sect->mActions[sect->mActions.Reserve(1)];
		act->mTitle = argv[1];
		act->mAction = argv[2];
	}
}
