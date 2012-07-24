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

#include "podtopod.h"

/**************************
 * track_list stuff
 **************************/

track_list *track_list_new(ipod_t ipod) {
   track_list *ret = (track_list *)malloc( sizeof( track_list ));
   ret->head = NULL;
   ret->ipod = ipod;
   return ret;
}

void track_list_add(track_list *lst, uint32_t track_id) {
	if (lst == NULL)
       return;
    
	track_item *nitem = (track_item *)malloc( sizeof( track_item ));
    nitem->track_id = track_id;
    nitem->next = NULL;
	
	if (lst->head == NULL) {
      lst->head = nitem;
	  lst->tail = nitem;
	  return;
	}

    /* add the item at the end of the list */	
	lst->tail->next = nitem;
	lst->tail = nitem;
}

uint32_t track_list_pop(track_list *lst) {
	if (lst == NULL || lst->head == NULL)
	   return 0;
	
	uint32_t ret = lst->head->track_id;
	track_item *ohead = lst->head;
	lst->head = ohead->next;
    free(ohead);
	return ret;
}

track_list *track_search_id(ipod_t ipod, param_list *param) {
	unsigned long numTracks, i;
	track_list *tlist; 
	
	if (!ipod)
      return NULL;

	numTracks = ipod_track_count(ipod);
    tlist = track_list_new(ipod);
	
	for (i=0;i<numTracks;i++) {
		ipod_track_t track = ipod_track_get_by_index(ipod,i);
		if (track) {
			uint32_t track_id;
            if (param_list_test_track(track, param)) {
			    track_id = ipod_track_get_attribute(track,IPOD_TRACK_ID);
				track_list_add(tlist, track_id);
			}
			ipod_track_free(track);
		} else {
			printf("Error: can't find track %d\n",i);
		}
	}

    return tlist;
}

/**************************
 * string_list stuff
 **************************/

string_list *string_list_new() {
   string_list *ret = (string_list *)malloc( sizeof( string_list ));
   ret->head = NULL;
   return ret;
}

void string_list_add(string_list *lst, char *string) {
	if (string == NULL || lst == NULL)
       return;
    
	string_item *nitem = (string_item *)malloc( sizeof( string_item ));
    nitem->string = string;
    nitem->next = NULL;
	
	if (lst->head == NULL) {
      lst->head = nitem;
	  lst->tail = nitem;
	  return;
	}

    /* add the item at the end of the list */	
	lst->tail->next = nitem;
	lst->tail = nitem;
}

void string_list_add_unique(string_list *lst, char *string) {
	if (string == NULL || lst == NULL)
       return;

	string_item *g = lst->head;
	while (g) {
	  if (strcmp(string, g->string) == 0)
	     return;
	  g = g->next;
    }
	
	string_list_add(lst, string);
}

char *string_list_pop(string_list *lst) {
	if (lst == NULL || lst->head == NULL)
	   return NULL;
	
	char *ret = lst->head->string;
	string_item *ohead = lst->head;
	lst->head = ohead->next;
    free(ohead);
	return ret;
}

string_list *string_list_track_search(ipod_t ipod, int tag, param_list *param) {
	unsigned long numTracks, i;
	string_list *tlist; 
	
	if (!ipod)
      return NULL;

	numTracks = ipod_track_count(ipod);
    tlist = string_list_new();
	
	for (i=0;i<numTracks;i++) {
		ipod_track_t track = ipod_track_get_by_index(ipod,i);
		if (track) {
			//uint32_t trackID;
            if (param_list_test_track(track, param)) {
                char *v = ipod_string_new();
			    v = ipod_track_get_text(track, tag, v);
				string_list_add_unique(tlist, v);
			}
			//trackID = ipod_track_get_attribute(track,IPOD_TRACK_ID);
			ipod_track_free(track);
		} else {
			printf("Error: can't find track %d\n",i);
		}
	}

    return tlist;
}

/**************************
 * param_list stuff
 **************************/

param_list *param_list_new() {
   param_list *ret = (param_list *)malloc( sizeof( param_list ));
   ret->head = NULL;
   return ret;
}

void param_list_add(param_list *lst, int tag, char *value) {
	if (lst == NULL || tag == -1)
       return;
    
	param_item *nitem = (param_item *)malloc( sizeof( param_item ));
    nitem->tag = tag;
	nitem->value = value;
    nitem->next = NULL;
	
	if (lst->head == NULL) {
      lst->head = nitem;
	  lst->tail = nitem;
	  return;
	}

    /* add the item at the end of the list */	
	lst->tail->next = nitem;
	lst->tail = nitem;
}

param_item *param_list_pop(param_list *lst) {
	if (lst == NULL || lst->head == NULL)
	   return NULL;
	
	param_item *ret = lst->head;
	lst->head = ret->next;

	return ret;
}

/* tests a track against a parameter list.
   returns 1 if it matches, 0 otherwise    */
int param_list_test_track(ipod_track_t track, param_list *param) {
    int ret = 1;
	if (track == NULL)
		return 0;
	if (param == NULL) /* all tracks satisfy the empty parameter list */
		return 1;
	
	param_item *i = param->head;
	
	while (i && ret) {
        char *s = NULL;
		s = ipod_track_get_text(track, i->tag, s);
        if (strcmp(s, i->value) != 0)
           ret = 0;
		ipod_string_free(s);
        i = i->next;
    }
	
	return ret;
}
