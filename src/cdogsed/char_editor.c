/*
    C-Dogs SDL
    A port of the legendary (and fun) action/arcade cdogs.
    Copyright (c) 2017 Cong Xu
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    Redistributions of source code must retain the above copyright notice, this
    list of conditions and the following disclaimer.
    Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.
*/
#include "char_editor.h"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_opengl.h>
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#define NK_SDL_GL2_IMPLEMENTATION
#include <nuklear/nuklear.h>
#include <nuklear/nuklear_sdl_gl2.h>
#include <cdogs/actors.h>
#include <cdogs/character.h>
#include <cdogs/log.h>

#define MAX_VERTEX_MEMORY 512 * 1024
#define MAX_ELEMENT_MEMORY 128 * 1024

#define ROW_HEIGHT 25
const float colRatios[] = { 0.3f, 0.7f };

typedef struct
{
	struct nk_context *ctx;
	Character *Char;
	CampaignSetting *Setting;
	EventHandlers *Handlers;
	int *FileChanged;
	char *CharacterClassNames;
	char *GunNames;
} EditorContext;

const float bg[4] = { 0.16f, 0.1f, 0.1f, 1.f };


static char *GetClassNames(const int len, const char *(*indexNameFunc)(int));
static const char *IndexCharacterClassName(const int i);
static int NumCharacterClasses(void);
static const char *IndexGunName(const int i);
static int NumGuns(void);
static int GunIndex(const GunDescription *g);
static bool HandleEvents(EditorContext *ec);
static void Draw(SDL_Window *win, EditorContext *ec);
void CharEditor(
	CampaignSetting *setting, EventHandlers *handlers, int *fileChanged)
{
	SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "1");
	SDL_Init(SDL_INIT_VIDEO);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);

	SDL_Window *win = SDL_CreateWindow("Character Editor",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		800, 600,
		SDL_WINDOW_OPENGL|SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE);

	SDL_GLContext glContext = SDL_GL_CreateContext(win);
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// Initialise editor context
	EditorContext ec;
	ec.ctx = nk_sdl_init(win);
	ec.Char = NULL;
	ec.Setting = setting;
	ec.Handlers = handlers;
	ec.FileChanged = fileChanged;
	ec.CharacterClassNames = GetClassNames(
		NumCharacterClasses(), IndexCharacterClassName);
	ec.GunNames = GetClassNames(NumGuns(), IndexGunName);

	// Initialise fonts
	struct nk_font_atlas *atlas;
	nk_sdl_font_stash_begin(&atlas);
	nk_sdl_font_stash_end();

	Uint32 ticksNow = SDL_GetTicks();
	Uint32 ticksElapsed = 0;
	for (;;)
	{
		Uint32 ticksThen = ticksNow;
		ticksNow = SDL_GetTicks();
		ticksElapsed += ticksNow - ticksThen;
		if (ticksElapsed < 1000 / FPS_FRAMELIMIT * 2)
		{
			SDL_Delay(1);
			continue;
		}

		if (!HandleEvents(&ec))
		{
			goto bail;
		}
		Draw(win, &ec);

		ticksElapsed -= 1000 / (FPS_FRAMELIMIT * 2);
	}

bail:
	nk_sdl_shutdown();
	CFREE(ec.CharacterClassNames);
	CFREE(ec.GunNames);
	//glDeleteTextures(1, (const GLuint *)&tex.handle.id);
	SDL_GL_DeleteContext(glContext);
	SDL_DestroyWindow(win);
}

static char *GetClassNames(const int len, const char *(*indexNameFunc)(int))
{
	int classLen = 0;
	for (int i = 0; i < (int)len; i++)
	{
		const char *name = indexNameFunc(i);
		classLen += strlen(name) + 1;
	}
	char *names;
	CMALLOC(names, classLen);
	char *cp = names;
	for (int i = 0; i < (int)len; i++)
	{
		const char *name = indexNameFunc(i);
		strcpy(cp, name);
		cp += strlen(name) + 1;
	}
	return names;
}

static const char *IndexCharacterClassName(const int i)
{
	const CharacterClass *c = IndexCharacterClass(i);
	return c->Name;
}
static int NumCharacterClasses(void)
{
	return
		gCharacterClasses.Classes.size + gCharacterClasses.CustomClasses.size;
}
static const char *IndexGunName(const int i)
{
	int j = 0;
	CA_FOREACH(const GunDescription, g, gGunDescriptions.Guns)
		if (!g->IsRealGun)
		{
			continue;
		}
		if (j == i)
		{
			return g->name;
		}
		j++;
	CA_FOREACH_END()
	CA_FOREACH(const GunDescription, g, gGunDescriptions.CustomGuns)
		if (!g->IsRealGun)
		{
			continue;
		}
		if (j == i)
		{
			return g->name;
		}
		j++;
	CA_FOREACH_END()
	CASSERT(false, "cannot find gun");
	return "";
}
static int NumGuns(void)
{
	int totalWeapons = 0;
	CA_FOREACH(const GunDescription, g, gGunDescriptions.Guns)
		if (g->IsRealGun)
		{
			totalWeapons++;
		}
	CA_FOREACH_END()
	CA_FOREACH(const GunDescription, g, gGunDescriptions.CustomGuns)
		if (g->IsRealGun)
		{
			totalWeapons++;
		}
	CA_FOREACH_END()
	return totalWeapons;
}
static int GunIndex(const GunDescription *g)
{
	int j = 0;
	CA_FOREACH(const GunDescription, gg, gGunDescriptions.Guns)
		if (!gg->IsRealGun)
		{
			continue;
		}
		if (g == gg)
		{
			return j;
		}
		j++;
	CA_FOREACH_END()
	CA_FOREACH(const GunDescription, gg, gGunDescriptions.CustomGuns)
		if (!g->IsRealGun)
		{
			continue;
		}
		if (g == gg)
		{
			return j;
		}
		j++;
	CA_FOREACH_END()
	CASSERT(false, "cannot find gun");
	return -1;
}

static bool HandleEvents(EditorContext *ec)
{
	SDL_Event e;
	nk_input_begin(ec->ctx);
	bool run = true;
	while (SDL_PollEvent(&e))
	{
		switch (e.type)
		{
			case SDL_KEYDOWN:
				if (e.key.repeat)
				{
					break;
				}
				KeyOnKeyDown(&ec->Handlers->keyboard, e.key.keysym);
				break;
			case SDL_KEYUP:
				KeyOnKeyUp(&ec->Handlers->keyboard, e.key.keysym);
				break;
			case SDL_QUIT:
				run = false;
				break;
			case SDL_WINDOWEVENT:
				switch (e.window.event)
				{
					case SDL_WINDOWEVENT_CLOSE:
						run = false;
						break;
					default:
						break;
				}
				break;
			default:
				break;
		}
		nk_sdl_handle_event(&e);
	}
	nk_input_end(ec->ctx);
	return run;
}

static int DrawClassSelection(
	EditorContext *ec, const char *label, const char *items, const int selected,
	const size_t len);
static void DrawCharColor(EditorContext *ec, const char *label, color_t *c);
static void DrawFlag(
	EditorContext *ec, const char *label, const int flag, const char *tooltip);
static void Draw(SDL_Window *win, EditorContext *ec)
{
	if (nk_begin(ec->ctx, "Character Store", nk_rect(10, 10, 240, 580),
		NK_WINDOW_BORDER|NK_WINDOW_TITLE))
	{
		// Show existing characters
		CA_FOREACH(Character, c, ec->Setting->characters.OtherChars)
			nk_layout_row_dynamic(ec->ctx, ROW_HEIGHT, 1);
			const int selected = ec->Char == c;
			char buf[256];
			sprintf(buf, "%s (%s)", c->Class->Name, c->Gun->name);
			// TODO: show character icon, use nk_select_image_label
			if (nk_select_label(ec->ctx, buf, NK_TEXT_LEFT, selected))
			{
				ec->Char = c;
			}
		CA_FOREACH_END()

		nk_layout_row_static(ec->ctx, 30, 80, 1);
		if (nk_button_label(ec->ctx, "Add +"))
		{
			ec->Char = CharacterStoreAddOther(&ec->Setting->characters);
			// set up character template
			ec->Char->Class = StrCharacterClass("Ogre");
			ec->Char->Colors.Skin = colorGreen;
			const color_t darkGray = {64, 64, 64, 255};
			ec->Char->Colors.Arms = darkGray;
			ec->Char->Colors.Body = darkGray;
			ec->Char->Colors.Legs = darkGray;
			ec->Char->Colors.Hair = colorBlack;
			ec->Char->speed = 256;
			ec->Char->Gun = StrGunDescription("Machine gun");
			ec->Char->maxHealth = 40;
			ec->Char->flags = FLAGS_IMMUNITY;
			ec->Char->bot->probabilityToMove = 50;
			ec->Char->bot->probabilityToTrack = 25;
			ec->Char->bot->probabilityToShoot = 2;
			ec->Char->bot->actionDelay = 15;

			*ec->FileChanged = true;
		}
		// TODO: delete button
	}
	nk_end(ec->ctx);

	if (ec->Char != NULL)
	{
		if (nk_begin(ec->ctx, "Character", nk_rect(260, 10, 240, 580),
			NK_WINDOW_BORDER|NK_WINDOW_TITLE))
		{
			nk_layout_row(ec->ctx, NK_DYNAMIC, ROW_HEIGHT, 2, colRatios);
			const int selectedClass = DrawClassSelection(
				ec, "Class:", ec->CharacterClassNames,
				CharacterClassIndex(ec->Char->Class), NumCharacterClasses());
			// TODO: draw heads as well, use nk_combo_begin_image_label /
			// nk_combo_item_image_label
			ec->Char->Class = IndexCharacterClass(selectedClass);

			// Character colours
			nk_layout_row(ec->ctx, NK_DYNAMIC, ROW_HEIGHT, 2, colRatios);
			DrawCharColor(ec, "Skin:", &ec->Char->Colors.Skin);
			DrawCharColor(ec, "Hair:", &ec->Char->Colors.Hair);
			DrawCharColor(ec, "Arms:", &ec->Char->Colors.Arms);
			DrawCharColor(ec, "Body:", &ec->Char->Colors.Body);
			DrawCharColor(ec, "Legs:", &ec->Char->Colors.Legs);

			// Speed (256 = 100%)
			nk_layout_row_dynamic(ec->ctx, ROW_HEIGHT, 1);
			int speedPct = ec->Char->speed * 100 / 256;
			nk_property_int(ec->ctx, "Speed (%):", 0, &speedPct, 400, 10, 1);
			ec->Char->speed = speedPct * 256 / 100;

			nk_layout_row(ec->ctx, NK_DYNAMIC, ROW_HEIGHT, 2, colRatios);
			const int selectedGun = DrawClassSelection(
				ec, "Gun:", ec->GunNames, GunIndex(ec->Char->Gun),
				NumGuns());
			// TODO: draw gun icons as well, use nk_combo_begin_image_label /
			// nk_combo_item_image_label
			ec->Char->Gun = IdGunDescription(selectedGun);

			nk_layout_row_dynamic(ec->ctx, ROW_HEIGHT, 1);
			nk_property_int(
				ec->ctx, "Max Health:", 10, &ec->Char->maxHealth, 1000, 10, 1);

			nk_layout_row_dynamic(ec->ctx, ROW_HEIGHT, 2);
			DrawFlag(ec, "Asbestos", FLAGS_ASBESTOS, "Immune to fire");
			DrawFlag(ec, "Immunity", FLAGS_IMMUNITY, "Immune to poison");
			DrawFlag(ec, "See-through", FLAGS_SEETHROUGH, NULL);
			DrawFlag(ec, "Runs away", FLAGS_RUNS_AWAY, "Runs away from player");
			DrawFlag(
				ec, "Sneaky", FLAGS_SNEAKY, "Shoots back when player shoots");
			DrawFlag(ec, "Good guy", FLAGS_GOOD_GUY, "Same team as players");
			DrawFlag(
				ec, "Sleeping", FLAGS_SLEEPING, "Doesn't move unless seen");
			DrawFlag(
				ec, "Prisoner", FLAGS_PRISONER, "Doesn't move until touched");
			DrawFlag(ec, "Invulnerable", FLAGS_INVULNERABLE, NULL);
			DrawFlag(ec, "Follower", FLAGS_FOLLOWER, "Follows players");
			DrawFlag(
				ec, "Penalty", FLAGS_PENALTY, "Large score penalty when shot");
			DrawFlag(ec, "Victim", FLAGS_VICTIM, "Takes damage from everyone");
			DrawFlag(
				ec, "Awake", FLAGS_AWAKEALWAYS,
				"Don't go to sleep after players leave");

			//enum {EASY, HARD};
			//static int op = EASY;

			/*nk_layout_row_static(ctx, 30, 80, 1);
			if (nk_button_label(ctx, "button"))
			{
				fprintf(stdout, "button pressed\n");
			}
			nk_layout_row_dynamic(ctx, 30, 2);
			if (nk_option_label(ctx, "easy", op == EASY)) op = EASY;
			if (nk_option_label(ctx, "hard", op == HARD)) op = HARD;*/

			/*nk_layout_row_dynamic(ctx, 20, 1);
			nk_label(ctx, "Image:", NK_TEXT_LEFT);
			nk_layout_row_static(
				ctx,
				tex_h, tex_w,
				1);
			nk_image(ctx, tex);*/
		}
		nk_end(ec->ctx);

		if (nk_begin(ec->ctx, "AI", nk_rect(510, 10, 250, 180),
			NK_WINDOW_BORDER|NK_WINDOW_TITLE))
		{
			nk_layout_row_dynamic(ec->ctx, ROW_HEIGHT, 1);
			nk_property_int(
				ec->ctx, "Move (%):", 0, &ec->Char->bot->probabilityToMove,
				100, 5, 1);
			nk_property_int(
				ec->ctx, "Track (%):", 0, &ec->Char->bot->probabilityToTrack,
				100, 5, 1);
			nk_property_int(
				ec->ctx, "Shoot (%):", 0, &ec->Char->bot->probabilityToShoot,
				100, 5, 1);
			nk_property_int(
				ec->ctx, "Action delay:", 0, &ec->Char->bot->actionDelay,
				50, 5, 1);
		}
		nk_end(ec->ctx);
	}

	/* Draw */
	int winWidth, winHeight;
	SDL_GetWindowSize(win, &winWidth, &winHeight);
	glViewport(0, 0, winWidth, winHeight);
	glClear(GL_COLOR_BUFFER_BIT);
	glClearColor(bg[0], bg[1], bg[2], bg[3]);

	// Blit the texture to screen
	//draw_tex(tex.handle.id, -0.5f, -0.5f, 1.f, 1.f);
	nk_sdl_render(NK_ANTI_ALIASING_ON, MAX_VERTEX_MEMORY, MAX_ELEMENT_MEMORY);

	// Display
	SDL_GL_SwapWindow(win);
}

static int DrawClassSelection(
	EditorContext *ec, const char *label, const char *items, const int selected,
	const size_t len)
{
	nk_label(ec->ctx, label, NK_TEXT_LEFT);
	int selectedNew = selected;
	// TODO: draw heads as well, use nk_combo_begin_image_label /
	// nk_combo_item_image_label
	nk_combobox_string(
		ec->ctx, items, &selectedNew, len,
		ROW_HEIGHT, nk_vec2(nk_widget_width(ec->ctx), 10 * ROW_HEIGHT));
	if (selectedNew != selected)
	{
		*ec->FileChanged = true;
	}
	return selectedNew;
}

static void DrawCharColor(EditorContext *ec, const char *label, color_t *c)
{
	nk_label(ec->ctx, label, NK_TEXT_LEFT);
	struct nk_color color = { c->r, c->g, c->b, 255 };
	const struct nk_color colorOriginal = color;
	if (nk_combo_begin_color(
		ec->ctx, color, nk_vec2(nk_widget_width(ec->ctx), 400)))
	{
		nk_layout_row_dynamic(ec->ctx, 100, 1);
		color = nk_color_picker(ec->ctx, color, NK_RGB);
		nk_layout_row_dynamic(ec->ctx, ROW_HEIGHT, 1);
		color.r = (nk_byte)nk_propertyi(ec->ctx, "#R:", 0, color.r, 255, 1, 1);
		color.g = (nk_byte)nk_propertyi(ec->ctx, "#G:", 0, color.g, 255, 1, 1);
		color.b = (nk_byte)nk_propertyi(ec->ctx, "#B:", 0, color.b, 255, 1, 1);
		nk_combo_end(ec->ctx);
		c->r = color.r;
		c->g = color.g;
		c->b = color.b;
		if (memcmp(&color, &colorOriginal, sizeof color))
		{
			*ec->FileChanged = true;
		}
	}
}

static void DrawFlag(
	EditorContext *ec, const char *label, const int flag, const char *tooltip)
{
	struct nk_rect bounds = nk_widget_bounds(ec->ctx);
	nk_checkbox_flags_label(ec->ctx, label, &ec->Char->flags, flag);
	if (tooltip && nk_input_is_mouse_hovering_rect(&ec->ctx->input, bounds))
	{
		nk_tooltip(ec->ctx, tooltip);
	}
}