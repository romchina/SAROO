
#include "main.h"
#include "ff.h"
#include <stdlib.h>
#include "cdc.h"


/******************************************************************************/


FIL track_fp[100];


/******************************************************************************/


static char *get_token(char **str_in)
{
	char *str, *start, match;

	if(str_in==NULL || *str_in==NULL)
		return NULL;
	str = *str_in;

	while(*str==' ' || *str=='\t') str++;

	if(*str=='"'){
		match = '"';
		start = str+1;
		str += 1;
	}else if(*str){
		match = ' ';
		start = str;
	}else{
		return NULL;
	}

	while(*str && *str!=match) str++;
	if(*str==match){
		*str = 0;
		*str_in = str+1;
	}else{
		*str_in = NULL;
	}

	return start;
}


char *get_line(u8 *buf, int *pos, int size)
{
	char *line = (char*)buf+*pos;
	
	if(*pos>=size)
		return NULL;

	while(*pos<size && (buf[*pos]!='\r' && buf[*pos]!='\n')) *pos = (*pos)+1;
	while(*pos<size && (buf[*pos]=='\r' || buf[*pos]=='\n')){
		buf[*pos] = 0;
		*pos = (*pos)+1;
	}

	return line;
}


/******************************************************************************/

static int sscfg_p = 0;

void ss_config_init(void)
{
	sscfg_p = 0;
	*(u32*)(SYSINFO_ADDR+0x0100) = 0;
}

void ss_config_put(u32 val)
{
	*(u32*)(SYSINFO_ADDR+0x0100+sscfg_p) = val;
	sscfg_p += 4;
	*(u32*)(SYSINFO_ADDR+0x0100+sscfg_p) = 0;
}

int get_gameid(char *gameid)
{
	FIL *fp = cdb.tracks[0].fp;
	u8 *fbuf = (u8*)0x24002000;
	int retv, i;
	u32 nread;

	retv = f_read(fp, fbuf, 256, &nread);
	if(retv)
		return -1;

	if(cdb.tracks[0].sector_size==2048){
		i = 0x20;
	}else{
		i = 0x30;
	}
	memcpy(gameid, fbuf+i, 16);
	gameid[16] = 0;

	for(i=15; i>=0; i--){
		if(gameid[i]==0x20){
			gameid[i] = 0;
		}else{
			break;
		}
	}

	return 0;
}


int parse_config(char *fname, char *gameid)
{
	FIL fp;
	u8 *fbuf = (u8*)0x24002000;
	char *p = NULL;
	int retv;

	ss_config_init();

	retv = f_open(&fp, fname, FA_READ);
	if(retv){
		return -2;
	}

	u32 nread;
	retv = f_read(&fp, fbuf, f_size(&fp), &nread);
	f_close(&fp);
	if(retv){
		return -3;
	}

	printk("\nLoad config [%s] for [%s]\n", fname, (gameid==NULL)?"Global":gameid);

	int cpos = 0;
	int g_sec = 0;
	int in_sec = 0;
	char *lbuf;
	while((lbuf=get_line(fbuf, &cpos, nread))!=NULL){
_next_section:
		//查找section: [xxxxxx]
		if(in_sec==0){
			if(lbuf[0]=='['){
				p = strrchr(lbuf, ']');
				if(p==NULL){
					return -4;
				}
				*p = 0;
				if(g_sec==0){
					// 第一个section一定是global
					if(strcmp(lbuf+1, "global")){
						return -5;
					}
					g_sec = 1;
					in_sec = 1;
					printk("Global config:\n");
				}else if(gameid && strcmp(lbuf+1, gameid)==0){
					// 找到了与game_id匹配的section
					in_sec = 1;
					printk("Game config:\n");
				}
			}
			continue;
		}else{
			if(lbuf[0]=='['){
				if(g_sec==1){
					g_sec = 2;
					in_sec = 0;
					if(gameid==NULL)
						break;
					goto _next_section;
				}else{
					break;
				}
			}
			if(strncmp(lbuf, "sector_delay", 12)==0){
				p = strchr(lbuf+12, '=');
				if(p==NULL) goto _invalid_config;
				sector_delay = strtoul(p+1, NULL, 10);
				printk("    sector_delay = %d\n", sector_delay);
			}
			if(strncmp(lbuf, "exmem_1M", 8)==0){
				printk("    exmem_1M\n");
				ss_config_put(0x30000001);
			}
			if(strncmp(lbuf, "exmem_4M", 8)==0){
				printk("    exmem_4M\n");
				ss_config_put(0x30000004);
			}
			if(strncmp(lbuf, "M_", 2)==0){
				int addr = strtoul(lbuf+2, &p, 16);
				p = strchr(lbuf+2, '=');
				if(p==NULL) goto _invalid_config;

				p += 1;
				while(*p==' ') p += 1;
				int width = strlen(p);
				int val = strtoul(p, NULL, 16);
				addr = ((width/2)<<28) | (addr&0x0fffffff);
				ss_config_put(addr);
				ss_config_put(val);
				printk("    M_%08x=%x\n", addr, val);
			}
			if(strncmp(lbuf, "lang_id", 7)==0){
				p = strchr(lbuf+7, '=');
				if(p==NULL) goto _invalid_config;
				lang_id = strtoul(p+1, NULL, 10);
				printk("    lang_id=%d\n", lang_id);
			}
			// TODO: more config here.
		}
	}
	printk("\n");

	return 0;

_invalid_config:
	printk("Invalid config line: {%s}\n", lbuf);
	led_event(LEDEV_SCFG_ERROR);
	return -1;
}

/******************************************************************************/

int parse_iso(char *fname)
{
	FIL *fp = &track_fp[0];
	TRACK_INFO *tk;
	int retv;

	retv = f_open(fp, fname, FA_READ);
	if(retv){
		return -2;
	}

	tk = &cdb.tracks[0];
	tk->fp = fp;
	tk->file_offset = 0;
	tk->sector_size = 2048;
	tk->fad_0 = 150;
	tk->fad_start = 150;
	tk->fad_end = 150 + (f_size(fp))/2048 - 1;
	tk->mode = 1;
	tk->ctrl_addr = 0x41;

	cdb.track_num = 1;
	init_toc();

	return 0;
}


int parse_cue(char *fname)
{
	FIL fp;
	u8 *fbuf = (u8*)0x24002000;
	char *dir_name  = (char*)0x2400a000;
	char *full_path = (char*)0x2400a200;
	int i, retv;

	FIL *tk_fp = NULL;
	TRACK_INFO *tk;
	int fad_offset = 150;
	int tno = -1;
	int last_tk = -1;

	retv = f_open(&fp, fname, FA_READ);
	if(retv){
		return -2;
	}

	u32 nread;
	retv = f_read(&fp, fbuf, f_size(&fp), &nread);
	f_close(&fp);
	if(retv){
		return -3;
	}

	strcpy(dir_name, fname);
	char *p = strrchr(dir_name, '/');
	*p = 0;


	int cpos = 0;
	char *lbuf;
	while((lbuf=get_line(fbuf, &cpos, nread))!=NULL){
		//printk("nread=%d cpos=%d lbuf: {%s}\n", nread, cpos, lbuf);
		char *token = get_token(&lbuf);
		//printk("token: {%s}\n", token);
		if(strcmp(token, "FILE")==0){
			char *tfile = get_token(&lbuf);
			char *ftype = get_token(&lbuf);
			if(tfile==NULL)
				return -4;
			if(strcmp(ftype, "BINARY"))
				return -5;

			sprintk(full_path, "%s/%s", dir_name, tfile);
			if(tno!=-1){
				// close last track
				tk = &cdb.tracks[tno];
				int fad_size = (f_size(tk_fp)-tk->file_offset)/tk->sector_size;
				tk->fad_end = tk->fad_start + fad_size - 1;
				fad_offset = tk->fad_end+1;
			}
			last_tk = tno+1;
			tk = &cdb.tracks[tno+1];
			tk_fp = &track_fp[tno+1];
			retv = f_open(tk_fp, full_path, FA_READ);
			if(retv){
				return -6;
			}
		}else if(strcmp(token, "TRACK")==0){
			char *tnum = get_token(&lbuf);
			int tid = strtoul(tnum, NULL, 10);
			tno += 1;
			if(tid!=tno+1){
				return -7;
			}

			char *mstr = get_token(&lbuf);
			int mode, size;
			if(strncmp(mstr, "MODE1", 5)==0){
				mode = 1;
				size = strtoul(mstr+6, NULL, 10);
			}else if(strncmp(mstr, "MODE2", 5)==0){
				mode = 2;
				size = strtoul(mstr+6, NULL, 10);
			}else if(strncmp(mstr, "AUDIO", 5)==0){
				mode = 3;
				size = 2352;
			}else{
				return -8;
			}

			tk = &cdb.tracks[tno];
			tk->fp = tk_fp;
			tk->sector_size = size;
			tk->mode = mode;
		}else if(strcmp(token, "INDEX")==0){
			char *inum = get_token(&lbuf);
			char *tstr = get_token(&lbuf);
			if(inum==NULL || tstr==NULL){
				return -9;
			}
			int idx = strtoul(inum, NULL, 10);
			int m = strtoul(tstr+0, &tstr, 10);
			int s = strtoul(tstr+1, &tstr, 10);
			int f = strtoul(tstr+1, &tstr, 10);
			int fad = MSF_TO_FAD(m, s, f);

			if(last_tk < tno){
				// close last track
				tk = &cdb.tracks[last_tk];
				tk->fad_end = fad_offset+fad-1;
				last_tk = tno;
			}

			tk = &cdb.tracks[tno];
			if(idx==0){
				tk->fad_0 = fad_offset + fad;
			}
			if(idx==1){
				tk->fad_start = fad_offset + fad;
				if(tk->fad_0==0)
					tk->fad_0 = tk->fad_start;
				tk->file_offset = fad*tk->sector_size;
				tk->ctrl_addr = (tk->mode==3)? 0x01 : 0x41;
			}
		}else if(strcmp(token, "PREGAP")==0){
		}else if(strcmp(token, "CATALOG")==0){
		}else{
			return -10;
		}
	}

	// close last track
	tk = &cdb.tracks[tno];
	int fad_size = (f_size(tk_fp)-tk->file_offset)/tk->sector_size;
	tk->fad_end = tk->fad_start + fad_size - 1;
	fad_offset = tk->fad_end+1;

	cdb.track_num = tno+1;

	init_toc();

	return 0;
}


/******************************************************************************/

int total_disc;
static int *disc_path = (int *)(IMGINFO_ADDR+4);
static char *path_str = (char*)(IMGINFO_ADDR);
static int pbptr;


int list_bins(int show)
{
	FRESULT retv;
	DIR dir;
	FILINFO *info;

	total_disc = 0;
	pbptr = 0x1000;
	memset(disc_path, 0x00, 0x20000-4);
	*(int*)(IMGINFO_ADDR) = 0;

	memset(&dir, 0, sizeof(dir));

	retv = f_opendir(&dir, "/SAROO/BIN");
	if(retv)
		return -1;

	info = malloc(sizeof(FILINFO));
	memset(info, 0, sizeof(*info));

	while(1){
		retv = f_readdir(&dir, info);
		if(retv!=FR_OK || info->fname[0]==0)
			break;
		if(info->fname[0]=='.')
			continue;

		if(!(info->fattrib & AM_DIR)){
			disc_path[total_disc] = pbptr;
			sprintk(path_str+pbptr, "/SAROO/BIN/%s", info->fname);
			pbptr += strlen(info->fname)+1+11;

			total_disc += 1;
			*(int*)(IMGINFO_ADDR) = total_disc;
		}
	}

	f_closedir(&dir);
	free(info);

	printk("Total discs: %d\n", total_disc);
	if(show){
		int i;
		for(i=0; i<total_disc; i++){
			printk(" %2d:  %s\n", i, path_str+disc_path[i]);
		}
		printk("\n");
	}

	return 0;
}


int list_disc(int show)
{
	FRESULT retv;
	DIR dir;
	FILINFO *info;

	total_disc = 0;
	pbptr = 0x1000;
	memset(disc_path, 0x00, 0x20000-4);
	*(int*)(IMGINFO_ADDR) = 0;

	memset(&dir, 0, sizeof(dir));

	retv = f_opendir(&dir, "/SAROO/ISO");
	if(retv)
		return -1;

	info = malloc(sizeof(FILINFO));
	memset(info, 0, sizeof(*info));

	while(1){
		retv = f_readdir(&dir, info);
		if(retv!=FR_OK || info->fname[0]==0)
			break;
		if(info->fname[0]=='.')
			continue;

		if(info->fattrib & AM_DIR){
			disc_path[total_disc] = pbptr;
			sprintk(path_str+pbptr, "/SAROO/ISO/%s", info->fname);
			pbptr += strlen(info->fname)+1+11;

			total_disc += 1;
			*(int*)(IMGINFO_ADDR) = total_disc;
		}
	}

	f_closedir(&dir);
	free(info);

	printk("Total discs: %d\n", total_disc);
	if(show){
		int i;
		for(i=0; i<total_disc; i++){
			printk(" %2d:  %s\n", i, path_str+disc_path[i]);
		}
		printk("\n");
	}

	return 0;
}


int unload_disc(void)
{
	int i;

	cdb.status = STAT_NODISC;
	set_status(cdb.status);

	if(cdb.track_num==0)
		return 0;

	for(i=0; i<cdb.track_num; i++){
		TRACK_INFO *tk = &cdb.tracks[i];
		if(tk->fp){
			f_close(tk->fp);
		}
		memset(tk, 0, sizeof(TRACK_INFO));
	}

	return 0;
}


int find_cue_iso(char *dirname, char *outname)
{
	FRESULT retv;
	DIR dir;
	FILINFO *info;
	int type = 0;

	memset(&dir, 0, sizeof(dir));
	retv = f_opendir(&dir, dirname);
	if(retv)
		return -1;

	info = malloc(sizeof(FILINFO));
	memset(info, 0, sizeof(*info));

	while(1){
		retv = f_readdir(&dir, info);
		if(retv!=FR_OK || info->fname[0]==0)
			break;
		if(info->fname[0]=='.')
			continue;

		if(info->fattrib & AM_DIR){
		}else{
			char *p = strrchr(info->fname, '.');
			if(strcmp(p, ".cue")==0){
				// 读到cue, 直接返回.
				sprintk(outname, "%s/%s", dirname, info->fname);
				type = 1;
				break;
			}else if(strcasecmp(p, ".iso")==0){
				// 读到iso, 还要继续看有没有cue.
				sprintk(outname, "%s/%s", dirname, info->fname);
				type = 2;
			}
		}
	}

	f_closedir(&dir);
	free(info);

	return type;
}


int load_disc(int index)
{
	int retv;
	char *fname;

	unload_disc();

	if(disc_path[index]==0){
		printk("Invalid disc index %d\n", index);
		return -1;
	}

	fname = malloc(256);
	retv = find_cue_iso(path_str+disc_path[index], fname);
	if(retv<0)
		goto _exit;

	printk("Load disc: {%s}\n", fname);
	if(retv==1){
		retv = parse_cue(fname);
	}else{
		retv = parse_iso(fname);
	}
	if(retv){
		printk("  retv=%d\n", retv);
	}else{
		char gameid[20];
		retv = get_gameid(gameid);
		if(retv){
			goto _exit;
		}
		parse_config("/saroocfg.txt", gameid);

		if(sector_delay_force>=0){
			sector_delay = sector_delay_force;
			printk("    force sector_delay = %d\n", sector_delay);
		}
	}

_exit:
	free(fname);
	return retv;
}

