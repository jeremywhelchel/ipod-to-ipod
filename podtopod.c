/*
 * Copyright (c) 2006 Jeremy Whelchel
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

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "pz.h"
#include "podtopod.h"
//#include "libipod/src/ipod_private.h" /* so we can read ipod->basePath */

#ifndef MAXPATHLEN
#define MAXPATHLEN 512
#endif

/********* Module-specific Settings ************/
#define PPSETTING_MODULE_ENABLED		0
#define PPSETTING_RIP_MOUNT_LOCATION	1
#define PPSETTING_LIP_SAVE_LOCATION		2

int rip_status;

static PzModule *pp_module;
static PzConfig *pp_config;


ipod_t	remote_ipod = NULL;
ipod_t  local_ipod  = NULL;

/* mpd dirty stuff */
static int mpd_dirty = 0;

int mpd_is_dirty() {
   int ret = mpd_dirty;
   mpd_dirty = 0;
   return ret;
}

void mpd_make_dirty() {
   mpd_dirty = 1;
}

void do_tracks() {
   /* Empty function to test stuff */
   if (pz_get_int_setting(pp_config, PPCONFIG_AUTOMOUNT))
     printf("Automount on\n");
   else
	 printf("Automount off\n");
}

/* Modified from Josh's about module */
char *humanize_size (unsigned long size) {
    static char buf[64];
    if (size >= 1048576) {
        sprintf (buf, "%ld.%01ld MB", size/1048576, size*10/1048576 % 10);
    } else if (size >= 1024) {
        sprintf (buf, "%ld.%01ld kB", size/1024, size*10/1024 % 10);
    } else {
        sprintf (buf, "%ld B", size);
    }
    return buf;
}

/****************************
 * Transfer Menu Stuff
 ****************************/

static TWindow *remove_tracks_aborted (ttk_menu_item *item) { return TTK_MENU_UPONE; }
static TWindow *remove_tracks_do_delete (ttk_menu_item *item) {
	printf("Deleting tracks:\n");
	track_list *tracks = (track_list *)item->data;
	ipod_t target_ipod = tracks->ipod;
    uint32_t track_id;
	while ((track_id = track_list_pop(tracks))) {
	   ipod_track_t track = ipod_track_get_by_track_id(target_ipod, track_id); 
	   char *filename = NULL;
	   filename = ipod_track_get_text(track, IPOD_FULL_PATH, filename);
	   printf("deleting track with ID=%i: %s\n", track_id, filename);
  	   /* I guess we can let the library delete the files */
	   //unlink(filename);
	   ipod_track_remove(track);
	   ipod_track_free(track);
	   ipod_string_free(filename);
	}

	ipod_flush(target_ipod);
	if (target_ipod == local_ipod)
		mpd_make_dirty();

	return TTK_MENU_UPALL;
}

static ttk_menu_item remove_tracks_menu[] = {
    { N_("No, don't delete anything."), { remove_tracks_aborted }, 0, 0 },
    { N_("Yes, remove all selected tracks."), { remove_tracks_do_delete }, TTK_MENU_ICON_EXE, 0 },
    { 0, {0}, 0, 0 }
};

static TWindow *remove_tracks (ttk_menu_item *item) {
    TWindow *ret = ttk_new_window();
    ret->data = 0x12345678;
    ttk_window_set_title (ret, _("Really Delete These Tracks?"));
    remove_tracks_menu[0].flags = 0; remove_tracks_menu[1].flags = TTK_MENU_ICON_EXE;
    remove_tracks_menu[0].data = remove_tracks_menu[1].data = item->data;
    ttk_add_widget (ret, ttk_new_menu_widget (remove_tracks_menu, ttk_textfont, item->menuwidth, item->menuheight));
    ttk_set_popup (ret);
    return ret;
}

static TWindow *transfer_tracks (ttk_menu_item *item) {
	printf("Transferring tracks:\n");
	track_list *tracks = (track_list *)item->data;
	ipod_t src_ipod = tracks->ipod;
	ipod_t dst_ipod;
	if (src_ipod == local_ipod)
		dst_ipod = remote_ipod;
	else if (src_ipod == remote_ipod)
		dst_ipod = local_ipod;
	else {
		pz_error("src_ipod not set in tracklist\n");
		return TTK_MENU_DONOTHING;
	}
	
	transfer_list_reset(src_ipod, dst_ipod);
	
	uint32_t track_id;
	while ((track_id = track_list_pop(tracks)))
	   transfer_list_add_track(track_id);
	
	/* This is ugly, but it is the only easy way I can get
	 * the first window to close */
	ttk_show_window(new_transfer_window());
	return TTK_MENU_UPONE;
}

typedef struct stat_data {
	int num_files;
	unsigned long total_bytes;
} stat_data;

void transfer_alt_stat_draw (PzWidget *wid, ttk_surface srf) {
	char buf[128];
	stat_data *stats = (stat_data *)wid->data;
    int y = wid->y + 5; 
	
	ttk_color color = ttk_ap_getx ("window.fg")->color;
	
    ttk_text (srf, ttk_textfont, wid->x + 5, y, color, _("Selected:"));
	y += ttk_text_height (ttk_textfont) + 1;

	sprintf(buf, "%i tracks.", stats->num_files);
    ttk_text (srf, ttk_textfont, wid->x + 5, y, color, buf);
/*
    ttk_text (srf, ttk_textfont, wid->x + 5, y, color, _("Tracks:"));
    sprintf(buf, "%i", stats->num_tracks);
	printf("textwid: %i\n", ttk_text_width(ttk_textfont, buf));
    ttk_text (srf, ttk_textfont, wid->w - ttk_text_width(ttk_textfont, buf) - 6,
                      y, color, buf);
*/	
	y += ttk_text_height (ttk_textfont) + 1;

	sprintf(buf, "%s bytes.", humanize_size(stats->total_bytes));
    ttk_text (srf, ttk_textfont, wid->x + 5, y, color, buf);
	y += ttk_text_height (ttk_textfont) + 1;
}

typedef struct enqueue_data {
	queue_fcn qfunc;
	ttk_menu_item *item;
} enqueue_data;

static TWindow *enqueue_tracks (ttk_menu_item *item) {
	enqueue_data *eqdata = (enqueue_data *)item->data;
	//printf("doing enqueue_tracks");
	ttk_menu_flash(item, 1);
	eqdata->qfunc(eqdata->item);
	return TTK_MENU_UPONE;
}

static ttk_menu_item transfer_menu_default[] = {
    { 0, { 0 }, 0, 0 },
    { N_("Transfer"), { transfer_tracks }, 0 /*TTK_MENU_ICON_EXE*/, 0 },
    { N_("Enqueue"), { enqueue_tracks }, 0, 0 },
    { N_("Remove"), { remove_tracks }, 0, 0 },
};

PzWindow *transfer_alt_menu(track_list *tlist, ipod_t src_ipod, queue_fcn qfunc, ttk_menu_item *qitem) {
	TWindow *ret;

	ret = pz_new_window(_("Transfer"), PZ_WINDOW_NORMAL);
    ret->data = 0x12345678;
    
	/* Show data up top: number of tracks, total size */	
	int num_tracks = 0, size=0;
	track_item *titem = tlist->head;
	
	while (titem) {
	   num_tracks++;
	   ipod_track_t track = ipod_track_get_by_track_id(src_ipod, titem->track_id);
	   size += ipod_track_get_attribute(track, IPOD_TRACK_SIZE);
       ipod_track_free(track);
	   titem = titem->next;
	}

	
	/* Add the status widget */
	PzWidget *stats_wid = pz_new_widget(transfer_alt_stat_draw, NULL);
	stat_data *stats = (stat_data *)malloc(sizeof(stat_data));
	stats->num_files = num_tracks;
	stats->total_bytes = size;
	stats_wid->data = stats;
	stats_wid->w = ret->w;
	ttk_add_widget(ret, stats_wid);
	
	
    /* Draw the Menu */	
    int menu_height = (ttk_text_height(ttk_menufont) + 4) * (qfunc? 3 : 2);
    TWidget *menu = ttk_new_menu_widget (transfer_menu_default, ttk_menufont, ttk_screen->w - ttk_screen->wx,
					menu_height);
	menu->y = ttk_screen->h - ttk_screen->wy - menu_height;
	transfer_menu_default[1].data = transfer_menu_default[3].data = tlist;
    if (remote_ipod)
		ttk_menu_append (menu, transfer_menu_default + 1);
	if (qfunc) {
		enqueue_data *eqdata = (enqueue_data *)malloc(sizeof(enqueue_data));		
		eqdata->qfunc = qfunc;
		eqdata->item = qitem;
		transfer_menu_default[2].data = eqdata;
		transfer_menu_default[2].free_data = 1;
		ttk_menu_append(menu, transfer_menu_default + 2);
	}
    ttk_menu_append (menu, transfer_menu_default + 3);
	ttk_add_widget(ret, menu);

	return pz_finish_window(ret);
}

void local_transfer_alt_menu(char *artist, char *album, char *song, queue_fcn qfunc, ttk_menu_item *qitem) {
    param_list *plist = param_list_new();
	if (artist) {
		param_list_add(plist, IPOD_ARTIST, artist);
		printf("Artist: %s, ", artist); }
	if (album) {
		param_list_add(plist, IPOD_ALBUM, album);
		printf("Album: %s, ", album); }
	if (song) {
		param_list_add(plist, IPOD_TITLE, song);
		printf("Song: %s, ", song); }

	track_list *tlist = track_search_id(local_ipod, plist);
	printf("Made list\n");
	ttk_show_window(transfer_alt_menu(tlist, local_ipod, qfunc, qitem));
	return;
}

/*****************************
 * File browser handler stuff
 *****************************/

#define PATHSIZ 512
static TWindow *browser_transfer (ttk_menu_item *item) {
   if (item->data == NULL)
     return TTK_MENU_DONOTHING;

   /* This is sketchy, the browser module actually does a change directory */
   char full_path[PATHSIZ];
   getcwd(full_path, PATHSIZ);
   if (full_path[strlen(full_path)-1] != '/')
	 strcat(full_path, "/");
   strcat(full_path, item->data);
   printf("full_path: %s\n", full_path);
   
   int remote_src = 0;
   if (strcmp(full_path, RIP_MOUNTPOINT) && rip_status == STATUS_MOUNTED)
     remote_src = 1;
   
   pz_error("File transferring not yet enabled");
   //ttk_show_window(transfer_alt_menu(NULL, remote_src ? remote_ipod : local_ipod, NULL, NULL));
   
   return TTK_MENU_DONOTHING;
}

int browser_transfer_predicate (const char *do_nothing) {
   printf("Pred sent: %s\n", do_nothing);
   return (rip_status == STATUS_MOUNTED ? 1 : 0);
}

static ttk_menu_item browser_transfer_menu_item = { N_("Transfer"), { browser_transfer }, 0, 0 };

//void pz_browser_add_action (int (*pred)(const char *), ttk_menu_item *action); // action->data will be set to file's full name
//void pz_browser_remove_action (int (*pred)(const char *));

/**************************
 * OTHER STUFF
 **************************/

PzWindow *new_browse_rip_window() {
	return pz_browser_open(RIP_MOUNTPOINT);
}

PzWindow *new_do_tracks_window() {
	do_tracks();
	return TTK_MENU_DONOTHING;
}

void draw_about_remote_ipod_widget (PzWidget *wid, ttk_surface srf) 
{
    // Draw the message
    int y = wid->y + 5;

	ttk_text (srf, ttk_textfont, wid->x + 5, y, ttk_makecol (BLACK), "Test");
	y += ttk_text_height (ttk_textfont) + 1;

	ttk_text (srf, ttk_textfont, wid->x + 5, y, ttk_makecol (BLACK), "Test2");
	y += ttk_text_height (ttk_textfont) + 1;
	
	if (check_kernel_module("tsb43aa82"))
 	ttk_text (srf, ttk_textfont, wid->x + 5, y, ttk_makecol (BLACK), "tsb43aa82 loaded");
	else
 	ttk_text (srf, ttk_textfont, wid->x + 5, y, ttk_makecol (BLACK), "tsb43aa82 NOT loaded");
	y += ttk_text_height (ttk_textfont) + 1;
	
	if (check_kernel_module("scsi_mod"))
 	ttk_text (srf, ttk_textfont, wid->x + 5, y, ttk_makecol (BLACK), "scsi_mod loaded");
	else
 	ttk_text (srf, ttk_textfont, wid->x + 5, y, ttk_makecol (BLACK), "scsi_mod NOT loaded");
	y += ttk_text_height (ttk_textfont) + 1;

	if (check_kernel_module("sd_mod"))
 	ttk_text (srf, ttk_textfont, wid->x + 5, y, ttk_makecol (BLACK), "sd_mod loaded");
	else
 	ttk_text (srf, ttk_textfont, wid->x + 5, y, ttk_makecol (BLACK), "sd_mod NOT loaded");
	y += ttk_text_height (ttk_textfont) + 1;
	
	if (check_kernel_module("sbp2"))
 	ttk_text (srf, ttk_textfont, wid->x + 5, y, ttk_makecol (BLACK), "sbp2 loaded");
	else
 	ttk_text (srf, ttk_textfont, wid->x + 5, y, ttk_makecol (BLACK), "sbp2 NOT loaded");
	y += ttk_text_height (ttk_textfont) + 1;
	
    y += 3;

    // Dividing line
    ttk_line (srf, wid->x + 5, y, wid->x + wid->w - 5, y, ttk_makecol (DKGREY));
    
    y += 3;
    // The message
#define MSG "Press a button to quit."
    ttk_text (srf, ttk_menufont, wid->x + (wid->w - ttk_text_width (ttk_menufont, MSG)) / 2,
	      wid->y + wid->h - ttk_text_height (ttk_menufont) - 5, ttk_makecol (BLACK), MSG);
}

int event_remote_ipod_widget (PzEvent *ev) {
    switch (ev->type) {
    	case PZ_EVENT_BUTTON_UP:
			pz_close_window (ev->wid->win);
			break;
    	case PZ_EVENT_DESTROY:
		//	ttk_free_surface (image);
		//	free (text);
			break;
    }
    return 0;
}


PzWindow *new_remote_ipod_info_window() {
	PzWindow *ret = pz_new_window("Remote iPod", PZ_WINDOW_NORMAL);
    pz_add_widget (ret, draw_about_remote_ipod_widget, event_remote_ipod_widget);
	return pz_finish_window(ret);
}

/*********************************************
 * Remote iPod Header Widget
 *********************************************/
static TWidget  *rip_icon = NULL;

static int rip_icon_timer_handler(PzWidget *this) {
#ifndef DO_POLL
	return 0;
#endif
	if (rip_status == STATUS_DISCONNECTED) {
		if ( poll_connection() )
	     ipod_status_connected();
    }
	else if (rip_status == STATUS_UNMOUNTED) {
       if ( !poll_connection() )
		 ipod_status_disconnected();
	   else if ( poll_mount(RIP_MOUNTDEVICE, RIP_MOUNTPOINT) )
         ipod_status_mounted();
    }   
    else if (rip_status == STATUS_MOUNTED) {
       if ( !poll_connection() )
		 ipod_status_disconnected();
	   else if ( !poll_mount(RIP_MOUNTDEVICE, RIP_MOUNTPOINT) )
         ipod_status_unmounted();
    }
	else if (rip_status == STATUS_TRANSFERRING) {
       /* These are trouble */
	   if ( !poll_connection() )
		 ipod_status_disconnected();
	   else if ( !poll_mount(RIP_MOUNTDEVICE, RIP_MOUNTPOINT) )
         ipod_status_unmounted();
	}
	else if (rip_status == STATUS_EJECTED) {
	   if ( !poll_connection() )
		 ipod_status_disconnected();
	}
	/*
	if (rip_status == STATUS_DISCONNECTED && connected) {
		printf("Connected\n");
		ipod_connected();
	}
	else if (rip_status == STATUS_MOUNTED && !connected) {
		printf("Disconnected\n");
		ipod_disconnected();
	}
	else if (rip_status == STATUS_TRANSFERRING && !connected)
		// this would be really bad
		return 0;
	*/
	return 0;
}

static void rip_icon_init_widget( PzWidget *widget ) {
	rip_icon = widget;
	rip_icon->w = 11;
	rip_icon->timer = rip_icon_timer_handler;
	ttk_widget_set_timer(rip_icon, 500);
	
}

static void rip_icon_update_widget( struct header_info * hdr ) {
}

static void rip_icon_draw_widget( struct header_info * hdr, ttk_surface srf ) {
	ttk_color color;
	PzWidget *this = hdr->widg;
	static int transfer_blink = 1;
	
	if (rip_icon == NULL)
		rip_icon_init_widget(this);

	/* horrible hack here */
	rip_icon_timer_handler( this );
	
	if (rip_status == STATUS_DISCONNECTED || rip_status == STATUS_EJECTED )
		return;
	
	if (rip_status == STATUS_TRANSFERRING && transfer_blink) {
		transfer_blink = 0;
		return;
	}
	transfer_blink = 1;

	#define COLOR_IPOD_VER (TTK_POD_PHOTO | TTK_POD_VIDEO | TTK_POD_NANO)
	if (rip_status == STATUS_TRANSFERRING && (ttk_get_podversion() & COLOR_IPOD_VER))
		color = ttk_makecol(255,10,10);
	else
 		color = ttk_ap_getx("header.fg")->color;
	
	ttk_rect	  (srf, this->x, this->y+1, this->x+this->w, this->y+this->h-6, color);	
	ttk_rect	  (srf, this->x+2, this->y+3, this->x+this->w-2, this->y+8, color);
	ttk_ellipse   (srf, this->x+5, this->y+11, 2, 2, color);

	if (rip_status != STATUS_UNMOUNTED) {
		ttk_rect  (srf, this->x+2, this->y+this->h-5, this->x+this->w-2, this->y+this->h-3, color);
		ttk_rect  (srf, this->x+4, this->y+this->h-3, this->x+this->w-4, this->y+this->h-1, color);
	}
}	

/********************
 * Status Transition
 ********************/

int is_connected(struct ttk_menu_item *item) { 
	return rip_status != STATUS_DISCONNECTED && rip_status != STATUS_EJECTED; }
int is_disconnected(struct ttk_menu_item *item) {
	return !is_connected(item); }
int is_mounted(struct ttk_menu_item *item) { 
	return rip_status == STATUS_MOUNTED || rip_status == STATUS_TRANSFERRING; }
int is_unmounted(struct ttk_menu_item *item) {
	return rip_status == STATUS_UNMOUNTED; }

/* Called when the iPod enteres the STATUS_UNMOUNTED state */
void ipod_status_connected() {
	rip_status = STATUS_UNMOUNTED;
	printf("Status: Connected (Unmounted)\n");

	pz_menu_add_after("/Remote iPod", NULL, "Extras");
	pz_menu_add_action("/Remote iPod/Browse Files", new_browse_rip_window)->visible = is_mounted;
	pz_menu_add_action("/Remote iPod/Info", new_remote_ipod_info_window)->visible = is_mounted;
#ifdef IPOD /* Horrible hack, make user unmount before ejecting */
	pz_menu_add_action("/Remote iPod/Eject", new_eject_window)->visible = is_unmounted;
#else
	pz_menu_add_action("/Remote iPod/Eject", new_eject_window)->visible = is_connected;
#endif
    pz_menu_add_action("/Remote iPod/Mount", new_domount_window)->visible = is_unmounted;
	pz_menu_add_action("/Remote iPod/Unmount", new_dounmount_window)->visible = is_mounted;

	if (poll_mount(RIP_MOUNTDEVICE, RIP_MOUNTPOINT))
  		ipod_status_mounted();
	else if (pz_get_int_setting(pp_config, PPCONFIG_AUTOMOUNT)) {
		printf("Attempting to automount\n");
		do_mount();
		if (poll_mount(RIP_MOUNTDEVICE, RIP_MOUNTPOINT))
	   		ipod_status_mounted();
	}
}

void load_rip_music() {
	char buf[256];
	struct stat s;

    if (remote_ipod) {
      pz_error("RiP already opened."); 
	  return;
	}

	strcpy(buf, RIP_MOUNTPOINT);
	strcat(buf, "/iPod_Control/iTunes/iTunesDB");
	if (stat(buf, &s) != 0) {
	  pz_error("Can't find remote iTunesDB.\nMusic not loaded.\n");
	  return;
	}
	
	remote_ipod = ipod_new(RIP_MOUNTPOINT);
	pz_menu_add_top("/Remote iPod/Music", NULL);
	pz_menu_add_action("/Remote iPod/Music/Artists", new_pp_artist_menu)->flags = TTK_MENU_ICON_SUB;
	pz_menu_add_action("/Remote iPod/Music/Albums", new_pp_album_menu)->flags = TTK_MENU_ICON_SUB;
    pz_menu_add_action("/Remote iPod/Music/Songs", new_pp_song_menu)->flags = TTK_MENU_ICON_SUB;
    pz_menu_add_action("/Remote iPod/Music/Playlists", new_pp_playlist_menu)->flags = TTK_MENU_ICON_SUB;
}

void close_rip_music() {
   if (!remote_ipod) {
     pz_error("No RiP to close.");
     return;
   }	   
   pz_menu_remove("/Remote iPod/Music/Artists");	
   pz_menu_remove("/Remote iPod/Music/Albums");
   pz_menu_remove("/Remote iPod/Music/Songs");
   pz_menu_remove("/Remote iPod/Music/Playlists");
   pz_menu_remove("/Remote iPod/Music");
   ipod_free(remote_ipod);
   remote_ipod = NULL;
}

/* Called when the ipod enters the STATUS_MOUNTED state */
void ipod_status_mounted() {
	printf("Status: Mounted\n");
    rip_status = STATUS_MOUNTED;
	
    if (pz_get_int_setting(pp_config, PPCONFIG_RIP_MUSIC))
	  load_rip_music();
}

void ipod_status_unmounted() {
	printf("Status: Unmounted\n");
	rip_status = STATUS_UNMOUNTED;
	
	if (remote_ipod)
	  close_rip_music();
}

void ipod_status_disconnected() {
    if (rip_status != STATUS_UNMOUNTED && rip_status != STATUS_EJECTED)
		ipod_status_unmounted();

	printf("Status: Disconnected\n");
	rip_status = STATUS_DISCONNECTED;

	pz_menu_remove("/Remote iPod/Browse Files");
	pz_menu_remove("/Remote iPod/Info");
	pz_menu_remove("/Remote iPod/Eject");
    pz_menu_remove("/Remote iPod/Mount");
	pz_menu_remove("/Remote iPod/Unmount");
	pz_menu_remove("/Remote iPod");	
}

void ipod_status_ejected() {
//    if (rip_status != STATUS_UNMOUNTED)
//		ipod_status_unmounted();
	ipod_status_disconnected();
	
	printf("Status: Ejected\n");
	rip_status = STATUS_EJECTED;
}

PzWindow *new_cycle_status_window() {
	if (rip_status == STATUS_DISCONNECTED)
		ipod_status_connected();
	else if (rip_status == STATUS_UNMOUNTED)
		ipod_status_mounted();
	else if (rip_status == STATUS_MOUNTED)
		rip_status = STATUS_TRANSFERRING;
	else if (rip_status == STATUS_TRANSFERRING)
		ipod_status_disconnected();

	return TTK_MENU_DONOTHING;
}

/***********************
 * Configuration Stuff *
 ***********************/

static int settings_button (TWidget *this, int key, int time) {
   if (key == TTK_BUTTON_MENU) {
     pz_save_config (pp_config);
     if (pz_get_int_setting(pp_config, PPCONFIG_RIP_MUSIC) && rip_status == STATUS_MOUNTED && !remote_ipod)
	   load_rip_music();
     else if (!pz_get_int_setting(pp_config, PPCONFIG_RIP_MUSIC) && rip_status == STATUS_MOUNTED && remote_ipod)
	   close_rip_music();
   }
   return ttk_menu_button (this, key, time);
}

/*********************************************
 * Module initialization and cleanup
 *********************************************/

static void cleanup_podtopod() {
	if (local_ipod)
	  ipod_free(local_ipod);
	if (remote_ipod)
	  ipod_free(remote_ipod);
	transfer_list_reset(NULL, NULL);
}

void init_podtopod() {
	pp_module = pz_register_module("podtopod", cleanup_podtopod);
	
	/* Initialize settings */
	pp_config = pz_load_config(pz_module_get_cfgpath(pp_module, "podtopod.conf"));
//	pz_menu_add_setting("/Settings/Pod-to-Pod/Enabled", PPCONFIG_ENABLED, pp_config, 0);
	pz_menu_add_setting("/Settings/Pod-to-Pod/Read RIP Music", PPCONFIG_RIP_MUSIC, pp_config, 0);
	pz_menu_add_setting("/Settings/Pod-to-Pod/Automount", PPCONFIG_AUTOMOUNT, pp_config, 0);
    ((TWidget *)pz_get_menu_item ("/Settings/Pod-to-Pod")->data)->button = settings_button;	

	//pz_menu_add_after("/Remote iPod", NULL, "Extras");
	
	rip_status = STATUS_DISCONNECTED;

	/* Add the status icon to the header */
	//rip_icon = ttk_new_widget(0,0);
	//rip_icon->w = 12;
	//rip_icon->h = 20;
	//rip_icon->draw = rip_icon_draw;
	//rip_icon->timer = rip_icon_timer_handler;
	//ttk_widget_set_timer(rip_icon, 500);

    pz_add_header_widget( "RIP Icon", rip_icon_update_widget, rip_icon_draw_widget, NULL );
	pz_enable_widget_on_side( HEADER_SIDE_RIGHT, "RIP Icon" );

	local_ipod = ipod_new(LIP_MOUNTPOINT);
	
	pz_menu_add_action("/RiPDB/Do Tracks", new_do_tracks_window);
	pz_menu_add_action("/RiPDB/Cycle Status", new_cycle_status_window);


	pz_browser_add_action(browser_transfer_predicate, &browser_transfer_menu_item);
}

PZ_MOD_INIT(init_podtopod)
