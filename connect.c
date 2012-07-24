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

//static char *ScsiDir = "/dev/scsi/host0/bus0/target0/lun0/";
/* disc, part1, part2, part3 */
/* /dev/sda   -- main partition
   /dev/sda1  -- firmware+bootloader
   /dev/sda2  -- data partition
   /dev/sda3  -- linux filesystem [if exists]
*/

//static char *ScsiDeviceFile = "/proc/bus/scsi";



// Checks whether a kernel module of the specified name is loaded
// TODO: Rewrite checking only /proc/modules
int check_kernel_module(char *module_name) {
	char *lsmod_cmd = "/bin/lsmod";

	size_t  len = 0;
	ssize_t read;
	char *buf = NULL;
	FILE *fp;
	
	if((fp = popen(lsmod_cmd, "r")) == NULL)
		return -1;
	
	getline(&buf, &len, fp); // first line is just header info
	
	while ((read = getline(&buf, &len, fp)) != -1)
		if (strncmp(buf, module_name, strlen(module_name)) == 0) {
		  	free(buf);
			pclose(fp);
     		return 1;
		}
	
	if (buf)
		free(buf);
	pclose(fp);

	return 0;
}

/*  poll_connection() - checks to see if there is an iPod
 *  connected to and recognized by the SCSI subsystem.
 *  Non-iPod (perhaps only works for 2.6 kernels with sysfs):
 *    Polls all devices in /sys/bus/scsi/devices/ *
 *  iPod version (uses SCSI procfs)
 *    Polls all devices in /proc/scsi/scsi
 */
#define ACCEPTED_MODEL				"iPod"
#define	ACCEPTED_VENDOR				"Apple"
#define SCSI_DIRNAME				"/sys/bus/scsi/devices"

#ifdef IPOD
/* Expected contents of /proc/scsi/scsi:
Attached devices:
Host: scsi0 Channel: 00 Id: 00 Lun:  00
Vendor: Apple    Model: iPod Rev: 1.62
*/
int poll_connection() {
#ifdef ALWAYS_CONNECTED
	return 1;
#endif	
    char buf[256];
    FILE *fp = fopen ("/proc/scsi/scsi", "r");
    if (!fp) 
	  return -1;
    
	int ret = 0;
	
    while (ret==0 && fgets (buf, 256, fp)) {
		char *vendor, *model;
        if (!(vendor = strstr(buf, "Vendor: "))) continue;
		if (!(model = strstr(buf, "Model: "))) continue;
		
		vendor += 8;
		model += 7;

		if (strncmp(vendor, ACCEPTED_VENDOR, strlen(ACCEPTED_VENDOR)) == 0)
		  if (strncmp(model, ACCEPTED_MODEL, strlen(ACCEPTED_MODEL)) == 0)
		    ret = 1;
    }
    fclose (fp);
	return ret;
}
#else
int poll_connection() {
	struct dirent 	*subdir;
	DIR  			*dir;
	FILE 			*file;
	char 			longname[512];

	dir = opendir(SCSI_DIRNAME);

	while ((subdir = readdir(dir))) {
		char model[64];
		char vendor[64];
		
		if (strncmp(subdir->d_name,".", 1 /*strlen(subdir->d_name)*/) == 0)	continue;
		
		/* Get model name */
		sprintf(longname, "%s/%s/model", SCSI_DIRNAME, subdir->d_name);
		file = fopen(longname, "r");
		if (!file) continue;
		fgets(model, 64, file); 
		fclose(file);
		if (model[strlen(model)-1]=='\n') model[strlen(model)-1] = '\0';		
		
		/* Get vendor name */
		sprintf(longname, "%s/%s/vendor", SCSI_DIRNAME, subdir->d_name);
		file = fopen(longname, "r");
		if (!file) continue;
		fgets(vendor, 64, file);
		fclose(file);
		if (vendor[strlen(vendor)-1]=='\n') vendor[strlen(vendor)-1] = '\0';		
		
		if (strncmp(model, ACCEPTED_MODEL, strlen(ACCEPTED_MODEL)) == 0)
			if (strncmp(vendor, ACCEPTED_VENDOR, strlen(ACCEPTED_VENDOR)) == 0) {
				return 1;				
			}
	}

	return 0;
}
#endif /* IPOD */

/* Polls to check if RiP is mounted.
 * Returns 0/1 for mounted/unmounted.
 * -1 is for error.
 * 2 is for mounted at incorrect location
 */
int poll_mount(const char *expected_device, const char *expected_mountpoint) {
#ifdef ALWAYS_MOUNTED
	return 1;
#endif	

    char buf[256];
    FILE *fp = fopen ("/proc/mounts", "r");
    if (!fp) 
	  return -1;
    
	int ret = 0;
	
    while (ret==0 && fgets (buf, 256, fp)) {
        if (!strchr (buf, ' ')) continue;

        char *mountpt = strchr (buf, ' ') + 1;
        //char *fstype = strchr (mountpt, ' ') + 1;

        *strchr (buf, ' ') = 0;
        *strchr (mountpt, ' ') = 0;
//        *strchr (fstype, ' ') = 0;
        if (strcmp (buf, expected_device)==0) {
           if (strcmp (mountpt, expected_mountpoint)==0)
	         ret = 1;
		   else {
			 pz_error("Warning: Remote ipod mounted at %s, expecting %s\n", mountpt, expected_mountpoint);
		     ret = 2;
		   }
	    }
    }
    fclose (fp);
	return ret;
}

int do_mount() {
	printf("Mounting %s at %s\n", RIP_MOUNTDEVICE, RIP_MOUNTPOINT);
	if (vfork() == 0) {
		/* This needs to wait for sbp2/scsi subsystem to recognize
		   all the proper partitions on the iPod
		   
		   Better yet: do checking to make sure proper partitions exists
					   this could also deal with FAT vs HFS iPods
		 */
#ifdef IPOD
		execl("/bin/mount", "/bin/mount", "-t", "vfat", RIP_MOUNTDEVICE, RIP_MOUNTPOINT, NULL);
#else
		execl("/bin/mount", "/bin/mount", RIP_MOUNTPOINT, NULL);
#endif
		_exit(0);
	}
	return 1;
}

int do_unmount() {
    if (!poll_mount(RIP_MOUNTDEVICE, RIP_MOUNTPOINT))
	  return 0;
	printf("Unmounting %s\n", RIP_MOUNTDEVICE);
	if (vfork() == 0) {
		execl("/bin/umount", "/bin/umount", RIP_MOUNTDEVICE, NULL);
		_exit(0);
	}
	return 1;
}

extern int rip_status;

int do_eject() {
	if (rip_status == STATUS_MOUNTED)
	  do_unmount();

	printf("Ejecting iPod at %s\n", RIP_MOUNTDEVICE);
#ifdef IPOD
	/* This is a poor hack to deal with busybox's eject not working for SCSI devices */
	if (vfork() == 0) { execl("/bin/rmmod", "/bin/rmmod", "sbp2", NULL); _exit(0); }
    pz_dialog("iPod Eject", "Please physically disconnect the iPods before pressing [OK]", 1, 0, "OK");
	if (vfork() == 0) { execl("/bin/modprobe", "/bin/modprobe", "sbp2", NULL); _exit(0); }
#else
	if (vfork() == 0) { execl("/usr/bin/eject", "/usr/bin/eject", RIP_MOUNTDEVICE, NULL); _exit(0); }
#endif

	ipod_status_ejected();
	return 1;
}

PzWindow *new_domount_window() {
	do_mount();
	return TTK_MENU_DONOTHING;
}

PzWindow *new_dounmount_window() {
	do_unmount();
	return TTK_MENU_DONOTHING;
}

PzWindow *new_eject_window() {
	do_eject();
	return TTK_MENU_UPALL;
}
