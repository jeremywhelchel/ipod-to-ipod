/*
 * Copyright (c) 2006 Jeremy Whelchel
 * Music menu hierarchy adapted from Courtney Cavin's mpdc module
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <string.h>
#include "podtopod.h"

static TWindow *open_song(ttk_menu_item *item) {
	int i, num = 0;
	ttk_menu_item *sitem;
	int state;

	param_list *plist = (param_list *)item->data;
   	if (plist == NULL)
		plist = param_list_new();
   	param_list_add(plist, IPOD_TITLE, (char *)item->name);
       
	track_list *tlist = track_search_id(remote_ipod, plist);
		
	return transfer_alt_menu(tlist, remote_ipod, NULL, NULL);
}

static int song_held_handler(TWidget *this, int button) {
	ttk_menu_item *item;
	if (button == TTK_BUTTON_ACTION) {
		item = ttk_menu_get_selected_item(this);
    	open_song(item);
	}
	return 0;
}

TWidget *populate_pp_songs(param_list *param) {
	TWidget *ret;

	char *song;
    string_list *tlist;

	ret = ttk_new_menu_widget(NULL, ttk_menufont,
			ttk_screen->w -	ttk_screen->wx,
			ttk_screen->h - ttk_screen->wy);
	ttk_menu_set_i18nable(ret, 0);

    tlist = string_list_track_search(remote_ipod, IPOD_TITLE, param);

	while ((song = string_list_pop(tlist))) {
		ttk_menu_item *item;
		item = (ttk_menu_item *)calloc(1, sizeof(ttk_menu_item));
		item->name = song;
		item->data = param;
		item->free_name = 1;
		item->makesub = open_song;
		ttk_menu_append(ret, item);
	}

	ret->held = song_held_handler;
	ret->holdtime = 500; /* ms */

	return ret;
}

PzWindow *new_pp_song_menu()
{
	PzWindow *ret;
	ret = pz_new_window(_("Songs"), PZ_WINDOW_NORMAL);
	ret->data = 0x12345678;
	ttk_add_widget(ret, populate_pp_songs(NULL));
	return pz_finish_window(ret);
}
