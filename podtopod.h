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
 
#ifndef _PODTOPOD_H_
#define _PODTOPOD_H_

#include "pz.h"
#include <ipod/ipod.h>
#include <ipod/ipod_io_file.h>
#include <ipod/ipod_constants.h>
#include <ipod/ipod_string.h>
//#include "libipod/src/ipod_atom.h"

#define DO_POLL 1 /* if 0 doesn't poll, only way to switch states is cycling */

//#define DO_MOUNT 1
//#define ALWAYS_MOUNTED
//#define ALWAYS_CONNECTED

#ifndef IPOD /* The following should reflect your configuration */
  #define LIP_MOUNTPOINT 	"/lip"
  #define RIP_MOUNTPOINT 	"/rip"
  #define RIP_MOUNTDEVICE  	"/dev/ipod2"
#else /* These probably shouldn't be changed */
  #define LIP_MOUNTPOINT 	"/mnt"
  #define RIP_MOUNTPOINT 	"/rip"
  #define RIP_MOUNTDEVICE  	"/dev/sda2"
//  #ifdef IPOD
//	#undef DO_POLL
//  #endif	
#endif /* IPOD */

#define STATUS_DISCONNECTED 		0
#define STATUS_UNMOUNTED 			1
#define STATUS_MOUNTED				2
#define STATUS_TRANSFERRING			3
#define STATUS_EJECTED				4

#define PPCONFIG_ENABLED			(1)
#define PPCONFIG_RIP_MUSIC			(2)
#define PPCONFIG_AUTOMOUNT			(3)

/**************************
 * param_list stuff
 **************************/

typedef struct param_item {
    int                tag;
	char              *value;
	struct param_item *next;
} param_item;

typedef struct param_list {
	param_item        *head, *tail;
} param_list;

param_list *param_list_new();
void param_list_add(param_list *lst, int tag, char *value);
param_item *param_list_pop(param_list *lst);
//void param_list_remove_tail

/**************************
 * track_list stuff
 **************************/

typedef struct track_item {
	uint32_t           track_id;
	struct track_item *next;
} track_item;

typedef struct track_list {
	track_item    *head, *tail;
	ipod_t		   ipod;
} track_list;

track_list *track_list_new(ipod_t ipod);
void track_list_add(track_list *lst, uint32_t track_id);
//void track_list_add_unique(track_list *lst, uint32_t track_id);
uint32_t track_list_pop(track_list *lst);
track_list *track_search_id(ipod_t ipod, param_list *param);

/**************************
 * string_list stuff
 **************************/

typedef struct string_item {
    char               *string;
	struct string_item *next;
} string_item;

typedef struct string_list {
	string_item        *head, *tail;
} string_list;

string_list *string_list_new();
void string_list_add(string_list *lst, char *string);
void string_list_add_unique(string_list *lst, char *string);
char *string_list_pop(string_list *lst);
string_list  *string_list_track_search(ipod_t ipod, int tag, param_list *param);
/**************************/



extern PzModule *module;

extern ipod_t remote_ipod, local_ipod;

char *humanize_size (unsigned long size);

void mpd_make_dirty();
int mpd_is_dirty();

PzWindow *new_pp_album_menu(void);
PzWindow *new_pp_artist_menu(void);
PzWindow *new_pp_queue_menu(void);
PzWindow *new_pp_song_menu(void);
PzWindow *new_pp_playlist_menu(void);
PzWindow *new_pp_genre_menu(void);

TWidget *populate_pp_albums(param_list *param);
TWidget *populate_pp_songs(param_list *param);

typedef void (*queue_fcn) ( ttk_menu_item *item );

/* 
Transfer_alt_menu

If tracks are selectd, they are given in tlist
If tracks are selected from mpdc module, qfunc and qitem are given to enable queueing
If just files are to be transferred, they are already 
*/
PzWindow *transfer_alt_menu(track_list *tlist, ipod_t src_ipod, queue_fcn qfunc, ttk_menu_item *qitem);

//PzWindow *transfer_alt_menu(track_list *tlist, ipod_t src_ipod, queue_fcn qfunc, ttk_menu_item *qitem);

void ipod_status_connected();
void ipod_status_mounted();
void ipod_status_unmounted();
void ipod_status_disconnected();
void ipod_status_ejected();

/* connect.c */
int check_kernel_module(char *module_name);
int poll_connection();
int poll_mount(const char *expected_device, const char *expected_mountpoint);
int do_unmount();
int do_eject();
int do_mount();
PzWindow *new_domount_window();
PzWindow *new_dounmount_window();
PzWindow *new_eject_window();

/* transfer.c */
PzWindow *new_transfer_window();
void transfer_list_reset(ipod_t src_ipod, ipod_t dst_ipod);
void transfer_list_add_file(char *src_filename, char *dst_filename, int size);
void transfer_list_add_track(uint32_t track_id);//ipod_track_t track);

void transfer_start();
//void transfer_stop();  /* as in disconnection or something */
void abort_transfer();

#endif /* _PODTOPOD_H_ */
