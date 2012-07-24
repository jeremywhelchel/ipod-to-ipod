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

#include "pz.h"
#include "podtopod.h"
#include "libipod/src/ipod_private.h" /* so we can read ipod->basePath */

extern int rip_status;

typedef struct transfer_item {
	char   			*src_filename;
	char   			*dst_filename; /* NULL for tracks */
	unsigned long	 filesize;
	
	/* The following are for music only, NULL otherwise */
	ipod_track_t 	 src_track;
	char            *dst_location;

	struct transfer_item *next;
} transfer_item;

static transfer_item  *trans_list_head;
static transfer_item  *trans_list_tail;
static ipod_t		   trans_list_dst_ipod;
static ipod_t          trans_list_src_ipod;
static int             total_files;
static unsigned long   total_bytes;

static PzWindow *transfer_window;
static PzWidget *transfer_widget;	

static transfer_item *cur_file;
static int            total_files_transferred;
static unsigned long  total_bytes_transferred;

static unsigned long  file_bytes_transferred;

static int            dst_ipod_dirty;

void transfer_list_reset(ipod_t src_ipod, ipod_t dst_ipod) {
	printf("Resetting transfer list.\n");
	cur_file = trans_list_head;
	while (cur_file) {
	  transfer_item *nxt = cur_file->next;
	  if (cur_file->src_track)
        ipod_track_free(cur_file->src_track);
	  free(cur_file);
	  cur_file=nxt;
	}
	
    total_bytes = 0;
    total_files = 0;
    trans_list_head = trans_list_tail = NULL;
    trans_list_src_ipod = src_ipod;
    trans_list_dst_ipod = dst_ipod;
}

void transfer_list_add_file(char *src_filename, char *dst_filename, int size) {
	transfer_item *nitem = (transfer_item *)malloc( sizeof( transfer_item ));
	nitem->src_filename = src_filename;
	nitem->dst_filename = dst_filename;
	nitem->filesize = size;
	nitem->src_track = NULL;
	nitem->dst_location = NULL;

	total_files++;
    total_bytes += nitem->filesize;	

    nitem->next = NULL;
	
	if (trans_list_head == NULL) {
      trans_list_head = nitem;
	  trans_list_tail = nitem;
	  return;
	}

    /* add the item at the end of the list */	
	trans_list_tail->next = nitem;
	trans_list_tail = nitem;
}

void transfer_list_add_track(uint32_t track_id) {

	ipod_track_t track = ipod_track_get_by_track_id(trans_list_src_ipod, track_id);	
	if (track == NULL) {
	    pz_error("Unable to find source track (ID=%i)\n", track_id);
		return;
	}
	
	/* create new track-specific transfer_item */
	transfer_item *nitem = (transfer_item *)malloc( sizeof( transfer_item ));
	nitem->src_track = track;
	nitem->src_filename = ipod_string_new();
	nitem->src_filename = ipod_track_get_text(track,IPOD_FULL_PATH,nitem->src_filename);
	nitem->filesize = ipod_track_get_attribute(track, IPOD_TRACK_SIZE);
	/* don't determine a destination filename until right before transfer */
	nitem->dst_filename = NULL;
	nitem->dst_location = NULL;

	total_files++;
    total_bytes += nitem->filesize;	

    nitem->next = NULL;
	
	if (trans_list_head == NULL) {
      trans_list_head = nitem;
	  trans_list_tail = nitem;
	  return;
	}

    /* add the item at the end of the list */	
	trans_list_tail->next = nitem;
	trans_list_tail = nitem;
}

/* Update attributes for new track
 * THESE NEED TO BE CHANGED WHENEVER THEY CHANGE IN THE LIBRARY 
 * Doesn't copy destination location */
static void copy_track_attributes(ipod_track_t src_track, ipod_track_t dst_track) {
	char *s;

	/* String attributes */
	#define COPY_TEXT_ATTRIBUTE(att) { s=ipod_string_new(); \
		        ipod_track_get_text(src_track, att, s);	\
		        if (s) { ipod_track_set_text(dst_track, att, s); } \
				}
	
	COPY_TEXT_ATTRIBUTE(IPOD_TITLE);
    /* no IPOD_LOCATION */
	COPY_TEXT_ATTRIBUTE(IPOD_ALBUM);
	COPY_TEXT_ATTRIBUTE(IPOD_ARTIST);
	COPY_TEXT_ATTRIBUTE(IPOD_GENRE);
	COPY_TEXT_ATTRIBUTE(IPOD_FILETYPE);
	COPY_TEXT_ATTRIBUTE(IPOD_EQSETTING);
	COPY_TEXT_ATTRIBUTE(IPOD_COMMENT);

	COPY_TEXT_ATTRIBUTE(IPOD_CATEGORY);
	COPY_TEXT_ATTRIBUTE(IPOD_COMPOSER);
	COPY_TEXT_ATTRIBUTE(IPOD_GROUPING);
	COPY_TEXT_ATTRIBUTE(IPOD_DESCRIPTION);
//	COPY_TEXT_ATTRIBUTE(IPOD_ENCLOSUREURL);
//	COPY_TEXT_ATTRIBUTE(IPOD_RSSURL);
//	COPY_TEXT_ATTRIBUTE(IPOD_CHAPTER);
//	COPY_TEXT_ATTRIBUTE(IPOD_SUBTITLE);
//	COPY_TEXT_ATTRIBUTE(IPOD_SMARTPLAYLIST_PREF);
//	COPY_TEXT_ATTRIBUTE(IPOD_SMARTPLAYLIST_DATA);
//	COPY_TEXT_ATTRIBUTE(IPOD_LIBRARYPLAYLIST_INDEX);
//	COPY_TEXT_ATTRIBUTE(IPOD_PLAYLIST_SETTINGS);
//	COPY_TEXT_ATTRIBUTE(IPOD_FULL_PATH);

	/* Numeric attributes */
	#define COPY_NUM_ATTRIBUTE(att) ipod_track_set_attribute(dst_track, att, ipod_track_get_attribute(src_track, att));

//	COPY_NUM_ATTRIBUTE(IPOD_TRACK_ID);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_VISIBLE);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_FILETYPE);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_VBR);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_COMPILATION);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_RATING);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_LAST_PLAYED_TIME);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_SIZE);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_DURATION);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_TRACK_NUMBER);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_TRACK_COUNT);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_YEAR);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_BIT_RATE);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_SAMPLE_RATE);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_VOLUME);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_START_TIME);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_END_TIME);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_SOUND_CHECK);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_PLAY_COUNT);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_ADDED_TIME);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_DISC_NUMBER);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_DISC_COUNT);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_USER_ID);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_LAST_MODIFICATION_TIME);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_BOOKMARK_TIME);
//	COPY_NUM_ATTRIBUTE(IPOD_TRACK_DBIDLO);
//	COPY_NUM_ATTRIBUTE(IPOD_TRACK_DBIDHI);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_CHECKED);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_APPLICATION_RATING);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_BEATS_PER_MINUTE);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_ARTWORK_COUNT);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_ARTWORK_SIZE);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_DBID2LO);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_DBID2HI);
	COPY_NUM_ATTRIBUTE(IPOD_TRACK_SAMPLE_COUNT);
//	COPY_NUM_ATTRIBUTE(IPOD_TRACK_LAST_PLAYED_TIME_NATIVE);
//	COPY_NUM_ATTRIBUTE(IPOD_TRACK_ADDED_TIME_NATIVE);
//	COPY_NUM_ATTRIBUTE(IPOD_TRACK_LAST_MODIFICATION_TIME_NATIVE);
}

/****************************
 * Copying Stuff
 ****************************/

/* progress bar borrowed from courtc's mpdc module */
static void fill_copied_percent(PzWidget *wid, ttk_surface srf, int per, int y){
	int num = per > 100 ? 100 : per;
	int w = wid->w - (wid->w / 6);
	int x = (wid->w - w) / 2;
	//int y = wid->h - 30;
	int pw = (w * num) / 100;
	const int h = 9;

	if (ttk_ap_get("music.bar.bg"))
		ttk_ap_fillrect(srf, ttk_ap_get("music.bar.bg"), x,y,x+w+1,y+h);

	ttk_ap_fillrect(srf, ttk_ap_get("music.bar"), x, y, x + pw + 1, y + h);

	ttk_ap_hline(srf, ttk_ap_get("window.fg"), x, x + w, y - 1);
	ttk_ap_hline(srf, ttk_ap_get("window.fg"), x, x + w, y + h);
	ttk_ap_vline(srf, ttk_ap_get("window.fg"), x - 1, y, y +(h - 1));
	ttk_ap_vline(srf, ttk_ap_get("window.fg"), x+w+1, y, y +(h - 1));
}

static void pp_place_text(PzWidget *wid, ttk_surface srf, void *text, int num) {
	int w, h;
	w = ttk_text_width(ttk_menufont,  (char *)text); 
	h = ttk_text_height(ttk_menufont);
	
	ttk_text(srf, ttk_menufont, w < wid->w ? (wid->w - w) / 2 : 8,
			h*num - (2*h)/3, ttk_ap_getx("window.fg")->color,
			(char *)text); 
}


/* Right now this is laid out to show up properly on standard B&W iPods.
 * this will probably need to be updated to look prettier on other iPod
 * generations later.
 */
static void draw_transfer_widget(PzWidget *wid, ttk_surface srf) {
	int posmod, i = 0;
	unsigned long percent;
	char sbuf[128];

	if (rip_status==STATUS_TRANSFERRING) {
	  
	  if (cur_file->src_track) {
		char *s = ipod_string_new();
        s = ipod_track_get_text(cur_file->src_track, IPOD_ARTIST, s);
		pp_place_text(wid, srf, s, 1);
        s = ipod_track_get_text(cur_file->src_track, IPOD_TITLE, s);
		pp_place_text(wid, srf, s, 2);
		ipod_string_free(s);		  
	  } else {
		sprintf(sbuf, "From: %s", cur_file->src_filename);
		pp_place_text(wid, srf, sbuf, 1);
	  	sprintf(sbuf, "To: %s", cur_file->dst_filename);
	  	pp_place_text(wid, srf, sbuf, 2);
	  }
	  
      sprintf(sbuf, "%s / ", humanize_size(file_bytes_transferred));
	  //sprintf(sbuf, "%s%s", sbuf, humanize_size(cur_file->filesize));
      strcat(sbuf, humanize_size(cur_file->filesize));
	  pp_place_text(wid, srf, sbuf, 3);
  	  percent = file_bytes_transferred * 100 / cur_file->filesize;
	} else {
	  if (cur_file)
		pp_place_text(wid, srf, "Transfer Aborted", 1);
	  else
	    pp_place_text(wid, srf, "Transfer Completed", 1);
	  pp_place_text(wid, srf, "Press [menu] to return", 2);
      percent = 100;
	}
		
	fill_copied_percent(wid, srf, percent, 46);

	if (total_files > 1) {
		sprintf(sbuf, "File %i of %i\n", total_files_transferred + (cur_file ? 1 : 0), total_files);
		pp_place_text(wid, srf, sbuf, 6);
		sprintf(sbuf, "%s / ", humanize_size(total_bytes_transferred));
		sprintf(sbuf, "%s%s total", sbuf, humanize_size(total_bytes));
		pp_place_text(wid, srf, sbuf, 7);	
	
		unsigned long tpercent;
		tpercent = total_bytes_transferred * 100 / total_bytes;
		//sprintf(sbuf, "%i%% Completed", tpercent);
		//pp_place_text(wid, srf, sbuf, 5);
		fill_copied_percent(wid, srf, tpercent, 97);
	}
}

static FILE *srcFile;
static FILE *dstFile;
static char *transfer_buffer = NULL;
#define BUFFER_SIZE		64*1024

static void start_copy() {
	int filesize;
	int len;
	struct stat st;
	
	transfer_buffer = (char *)malloc(BUFFER_SIZE);
	if (!transfer_buffer) {
		pz_error("Unable to alloc transfer buffer!\n");
		return;
	}

	dst_ipod_dirty = 0;
	total_files_transferred = 0;
	total_bytes_transferred = 0;
	file_bytes_transferred = 0;
	cur_file = trans_list_head;
	
	rip_status = STATUS_TRANSFERRING;
}

/* assumes cur_file is already pointing to proper file */
static void start_next_file() {
	printf("Starting next file: %s\n", cur_file->src_filename);
	
	struct stat s;
	if (stat(cur_file->src_filename, &s) != 0) {
	  pz_error("Cannot stat %s\n", cur_file->src_filename);
	  return;
	}
	

	/* If track is being transferred, get a new destination filename */
	if (cur_file->src_track) {
		if (cur_file->dst_filename) {
		  pz_error("Track shouldn't already have dst_filename\n");
		  abort_transfer();
		  return;
		}
		
		cur_file->dst_filename = (char *)malloc(256);
		char *dst_base = ((ipod_p)(trans_list_dst_ipod))->basePath;
	    char *extension, *p;
		extension = (char*)ipod_extension_of(cur_file->src_filename,".mp3");
		cur_file->dst_location = ipod_unique_track_location_wrapper(trans_list_dst_ipod, extension);
    	strcpy(cur_file->dst_filename, dst_base);
		strcat(cur_file->dst_filename, cur_file->dst_location);
		while((p=strpbrk(cur_file->dst_filename,":"))!=NULL) *p='/'; /* convert ':'s to '/' */
	}
	
	if (!cur_file->dst_filename) {
	  pz_error("No destination filename given!\n");
	  abort_transfer();
	  return;
	}

	srcFile = fopen(cur_file->src_filename, "r");
	if (!srcFile) {
	  pz_error("Could not open %s for reading\n", cur_file->src_filename);
	  abort_transfer();
	  return;
	}
	
	dstFile = fopen(cur_file->dst_filename, "w");
	if (!dstFile) {
	  pz_error("Could not open %s for writing\n", cur_file->dst_filename);
	  fclose(srcFile);
	  srcFile = NULL;
	  abort_transfer();
	  return;
	}
}

static void finish_file() {
	/* For track files, update destination iTunesDB
	 * We don't write (flush) it yet, we'll do that after
     * we transfer all the files. */
	if (cur_file->src_track) {
	  ipod_track_t dst_track = ipod_track_add(trans_list_dst_ipod);
	  copy_track_attributes(cur_file->src_track, dst_track);
      ipod_track_set_text(dst_track, IPOD_LOCATION, cur_file->dst_location);
	  ipod_track_free(dst_track);
	  dst_ipod_dirty = 1;
	}

	total_files_transferred += 1;
	
	fclose(srcFile);
	srcFile = NULL;
	fclose(dstFile);
	dstFile = NULL;
	
	/* move on to next file */
	cur_file = cur_file->next;
	file_bytes_transferred = 0;
}


static void transfer_finished() {
	printf("Transfer finished!\n");

	free(transfer_buffer);
	if (srcFile) {
	  fclose(srcFile);
	  srcFile = NULL;
	}	
	if (dstFile) {
	  fclose(dstFile);
	  dstFile = NULL;
    }

	
	if (dst_ipod_dirty) {
      ipod_flush(trans_list_dst_ipod);
	  if (trans_list_dst_ipod == local_ipod)
		mpd_make_dirty();
	}
		
	rip_status = STATUS_MOUNTED;
	ttk_widget_set_timer(transfer_widget, 0); /* unset timer */
}

static void do_copy() {
    unsigned long bytesRead,bytesWritten;
    bytesRead = fread(transfer_buffer,1,BUFFER_SIZE,srcFile);
    if (bytesRead==0) {
	  pz_error("Zero bytes read!\n");
	  abort_transfer();
	  return;
	}
    
	bytesWritten = fwrite(transfer_buffer,1,bytesRead,dstFile);
    if (bytesRead != bytesWritten) {
	  pz_error("bytesRead != bytesWritten!\n");
	  abort_transfer();
	  return;
	}
    file_bytes_transferred += bytesWritten;
	total_bytes_transferred += bytesWritten;
	
    return;
}

static void close_transfer() {
	transfer_list_reset(NULL, NULL);
	pz_close_window(transfer_window);
}

void abort_transfer() {
	transfer_finished();
}

int transfer_widget_event (PzEvent *ev) {
    switch (ev->type) {
		case PZ_EVENT_BUTTON_DOWN:
//			if (ev->arg == PZ_BUTTON_ACTION)
//				pz_dialog("title", "no press", 2, 0, "but a", "but b");
//			else 
		    if (ev->arg == PZ_BUTTON_MENU) {
			    if (rip_status == STATUS_TRANSFERRING) {
				  if (pz_dialog("Alert!", "Really abort transfer?", 2, 0, "No", "Yes") == 1)
				    abort_transfer();
			    }  
				else
				  close_transfer();  
				//pz_close_window(ev->wid->win);
			}
			break;
    	case PZ_EVENT_BUTTON_UP:
			//pz_close_window (ev->wid->win);
			break;
    	case PZ_EVENT_DESTROY:
		//	ttk_free_surface (image);
		//	free (text);
			break;
    }
    return 0;
}

static int transfer_widget_timer_handler(PzWidget *this) {
//	printf("transfer widget timer called\n");

	if (!cur_file->dst_filename)
	  start_next_file();

	do_copy();
	transfer_widget->dirty = 1;
	
	if (cur_file->filesize == file_bytes_transferred)
	  finish_file();
	
	if (!cur_file)
	  transfer_finished();
	
	return 0;
}

PzWindow *new_transfer_window() {
	PzWindow *ret;
	PzWidget *wid;	
	
    ret = pz_new_window(_("Transferring File"), PZ_WINDOW_NORMAL);
	wid = pz_add_widget(ret, draw_transfer_widget, transfer_widget_event);
	ttk_widget_set_timer(wid, 1);
	wid->timer = transfer_widget_timer_handler;

	transfer_window = ret;
	transfer_widget = wid;

	start_copy();
	
	return pz_finish_window(ret);
}
