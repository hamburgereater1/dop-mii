#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dir.h>
#include <sys/stat.h>
#include <malloc.h>
#include <ctype.h>
#include <ogcsys.h>
#include <wiiuse/wpad.h>

#include "Controller.h"
#include "FileSystem.h"
#include "Title.h"
#include "Tools.h"
#include "Savegame.h"
#include "Video.h"

#define round_up(x,n)	(-(-(x) & -(n)))

/* Variables */
static char buffer[1024] ATTRIBUTE_ALIGN(32);

static Controller Controlller;

u64 StrToHex64(const char *str)
{
	u64 val = 0;
	u32 cnt, len;

	/* String length */
	len = strlen(str);

	for (cnt = 0; cnt < len; cnt++) {
		u32  idx = len - (cnt + 1);
		char c   = toupper(str[idx]);

		u64 n = (isdigit(c)) ? c - '0' : (c - 'A') + 0xA;
		u64 m = 1;

		for (idx = 0; idx < cnt; idx++)
			m *= 16;

		/* Convert to hex */
		val += n * m;
	}

	return val;
} 

s32 __Savegame_CopyData(const char *srcpath, const char *dstpath)
{
	FILE     *infp = NULL, *outfp = NULL;
	DIR_ITER *dir  = NULL;

	char        filename[1024];
	struct stat filestat;

	s32 ret = 0;

	/* Create directory */
	mkdir(dstpath, 777);

	/* Open source directory */
	dir = diropen(srcpath);
	if (!dir)
		return -1;

	/* Get directory entries */
	while (!dirnext(dir, filename, &filestat)) {
		char inpath[128], outpath[128];

		/* Ignore invalid entries */
		if (!strcmp(filename, ".") || !strcmp(filename, ".."))
			continue;

		/* Generate paths */
		sprintf(inpath,  "%s/%s", srcpath, filename);
		sprintf(outpath, "%s/%s", dstpath, filename);

		/* Directory/File check */
		if (filestat.st_mode & S_IFDIR) {
			/* Copy directory */
			ret = __Savegame_CopyData(inpath, outpath);
			if (ret < 0)
				goto out;
		} else {
			/* Open input/output file */
			infp  = fopen(inpath,  "rb");
			outfp = fopen(outpath, "wb");
			if (!infp || !outfp) {
				ret = -1;
				goto out;
			}

			for (;;) {
				/* Read data */
				ret = fread(buffer, 1, sizeof(buffer), infp);
				if (ret < 0)
					goto out;

				/* EOF */
				if (!ret)
					break;

				/* Write data */
				ret = fwrite(buffer, 1, ret, outfp);
				if (ret < 0)
					goto out;
			}

			/* Close files */
			fclose(infp);
			fclose(outfp);
		}
	}

out:
	/* Close files */
	if (infp)
		fclose(infp);
	if (outfp)
		fclose(outfp);

	/* Close directory */
	if (dir)
		dirclose(dir);

	return ret;
}


s32 Savegame_GetNandPath(u64 tid, char *outbuf)
{
	s32 ret;

	/* Get data directory */
	ret = ES_GetDataDir(tid, buffer);
	if (ret < 0)
		return ret;

	/* Generate NAND directory */
	sprintf(outbuf, "isfs:%s", buffer);

	return 0;
}

s32 Savegame_GetTitleName(const char *path, char *outbuf)
{
	FILE *fp = NULL;

	char filepath[128];
	u16  buffer[65];
	u32  cnt;

	/* Generate filepath */
	sprintf(filepath, "%s/banner.bin", path);

	/* Open banner */
	fp = fopen(filepath, "rb");
	if (!fp)
		return -1;

	/* Read name */
	fseek(fp, 32, SEEK_SET);
	fread(buffer, sizeof(buffer), 1, fp);

	/* Close file */
	fclose(fp);

	/* Copy name */
	for (cnt = 0; buffer[cnt]; cnt++)
		outbuf[cnt] = buffer[cnt];

	outbuf[cnt] = 0;

	return 0;
}

s32 Savegame_CheckTitle(const char *path)
{
	FILE *fp = NULL;

	char filepath[128];

	/* Generate filepath */
	sprintf(filepath, "%s/banner.bin", path);

	/* Try to open banner */
	fp = fopen(filepath, "rb");
	if (!fp)
		return -1;

	/* Close file */
	fclose(fp);

	return 0;
}

s32 Savegame_Manage(u64 tid, u32 mode, const char *devpath)
{
    Title *SaveGameTitle = new Title(tid);

	char nandpath[128];
	s32  ret;

	/* Get NAND path */
	ret = Savegame_GetNandPath(SaveGameTitle->TitleId, nandpath);
	if (ret < 0)
		return ret;

	/* Manage savegame */
	switch (mode) {
	case SAVEGAME_EXTRACT:
		ret = __Savegame_CopyData(nandpath, devpath);
		break;

	case SAVEGAME_INSTALL:
	    ret = __Savegame_CopyData(devpath, nandpath);
		break;

	default:
		return -1;
	}

	return ret;
}

//Title stuffs was here

s32 __Menu_GetNandSaves(struct savegame **outbuf, u32 *outlen)
{
    Title *SaveGameTitle;

	struct savegame *buffer = NULL;

	u64 *titleList = NULL;
	u32  titleCnt;

	u32 cnt, idx;
	s32 ret;

	/* Get title list */
	ret = SaveGameTitle->GetList(&titleList, &titleCnt);
	if (ret < 0)
		return ret;

	/* Allocate memory */
	buffer = (savegame*)malloc(sizeof(struct savegame) * titleCnt);
	if (!buffer) {
		ret = -1;
		goto out;
	}

	/* Copy titles */
	for (cnt = idx = 0; idx < titleCnt; idx++) {
		u64  tid = titleList[idx];
		char savepath[128];

		/* Generate dirpath */
		Savegame_GetNandPath(tid, savepath);

		/* Check for title savegame */
		ret = Savegame_CheckTitle(savepath);
		if (!ret) {
			struct savegame *save = &buffer[cnt++];

			/* Set title name */
			Savegame_GetTitleName(savepath, save->name);

			/* Set title ID */
			save->tid = tid;
		}
	}

	/* Set values */
	*outbuf = buffer;
	*outlen = cnt;

	/* Success */
	ret = 0;

out:
	/* Free memory */
	if (titleList)
		free(titleList);

	return ret;
}

s32 __Menu_GetDeviceSaves(struct savegame **outbuf, u32 *outlen, s32 device)
{
	struct savegame *buffer = NULL;
	DIR_ITER        *dir    = NULL;

	char dirpath[1024], filename[1024];
	u32  cnt = 0;

	/* Generate dirpath */
	sprintf(dirpath, "%s:" SAVES_DIRECTORY, deviceList[device].mount);

	/* Open directory */
	dir = diropen(dirpath);
	if (!dir)
		return -1;

	/* Count entries */
	for (cnt = 0; !dirnext(dir, filename, NULL); cnt++);

	/* Entries found */
	if (cnt > 0) {
		/* Allocate memory */
		buffer = (savegame*)malloc(sizeof(struct savegame) * cnt);
		if (!buffer) {
			dirclose(dir);
			return -2;
		}

		/* Reset directory */
		dirreset(dir);

		/* Get entries */
		for (cnt = 0; !dirnext(dir, filename, NULL);) {
			char savepath[128];
			s32  ret;

			/* Generate dirpath */
			sprintf(savepath, "%s/%s", dirpath, filename);

			/* Check for title savegame */
			ret = Savegame_CheckTitle(savepath);
			if (!ret) {
				struct savegame *save = &buffer[cnt++];

				/* Set title name */
				Savegame_GetTitleName(savepath, save->name);

				/* Set title ID */
				save->tid = StrToHex64(filename);
			}
		}
	}

	/* Close directory */
	dirclose(dir);

	/* Set values */
	*outbuf = buffer;
	*outlen = cnt;

	return 0;
}

s32 __Menu_EntryCmp(const void *p1, const void *p2)
{
	struct savegame *s1 = (struct savegame *)p1;
	struct savegame *s2 = (struct savegame *)p2;

	/* Compare entries */
	return strcmp(s1->name, s2->name);
}

s32 __Menu_RetrieveList(struct savegame **outbuf, u32 *outlen, s32 mode, s32 device)
{
	s32 ret;

	switch (mode) {
	case SAVEGAME_EXTRACT:
		/* Retrieve from NAND */
		ret = __Menu_GetNandSaves(outbuf, outlen);
		break;

	case SAVEGAME_INSTALL:
		/* Retrieve from device */
		ret = __Menu_GetDeviceSaves(outbuf, outlen, device);
		break;

	default:
		return -1;
	}

	/* Sort list */
	if (ret >= 0)
		qsort(*outbuf, *outlen, sizeof(struct savegame), __Menu_EntryCmp);

	return ret;
}


s32 Menu_Device(void)
{
	fatDevice *dev = NULL;
	s32 device = 0;

	char dirpath[128];
	s32  ret;
	
	u32 buttons = 0;

	/* Select source device */
	for (;;) {
		/* Selected device */
		dev = &deviceList[device];
		
		VIDEO_WaitVSync();
	    Console::ClearScreen();

		printf("\t>> Select storage device: < %s >\n\n", dev->name);

		printf("\t   Press LEFT/RIGHT to change the selected device.\n\n");

		printf("\t   Press A button to continue.\n");
		printf("\t   Press HOME button to restart.\n\n\n");

		Controlller.ScanPads(&buttons);

		/* LEFT/RIGHT buttons */
		if (buttons & WPAD_BUTTON_LEFT) {
			if ((--device) <= -1)
				device = (NB_DEVICES - 1);
		}
		if (buttons & WPAD_BUTTON_RIGHT) {
			if ((++device) >= NB_DEVICES)
				device = 0;
		}

		/* HOME button */
		if (buttons & WPAD_BUTTON_HOME)
			exit(0);

		/* A button */
		if (buttons & WPAD_BUTTON_A)
			break;
			
		VIDEO_WaitVSync();
	}

	printf("[+] Mounting device, please wait...");
	fflush(stdout);

	/* Mount device */
	ret = dev->interface->startup();
	fatMountSimple(dev->mount, dev->interface);
	if (ret < 0) {
		printf(" ERROR! (ret = %d)\n", ret);
		goto err;
	} else
		printf(" OK!\n");

	/* Create savegames directory */
	sprintf(dirpath, "%s:" SAVES_DIRECTORY, dev->mount);
	mkdir(dirpath, 777);

	return device;

err:
	/* Unmount device */
	fatUnmount(dev->mount);
	dev->interface->shutdown();

	printf("\n");
	printf("    Press any button to continue...\n");

	Controlller.WaitAnyKey();

	/* Prompt menu again */
	Menu_Device();
	
	return -1;
}

