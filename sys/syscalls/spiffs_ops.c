/*
 * Whitecat, SPIFFS wrapper functions
 *
 * Copyright (C) 2015 - 2016
 * IBEROXARXA SERVICIOS INTEGRALES, S.L. & CSS IBÉRICA, S.L.
 * 
 * Author: Jaume Olivé (jolive@iberoxarxa.com / jolive@whitecatboard.org)
 * 
 * All rights reserved.  
 *
 * Permission to use, copy, modify, and distribute this software
 * and its documentation for any purpose and without fee is hereby
 * granted, provided that the above copyright notice appear in all
 * copies and that both that the copyright notice and this
 * permission notice and warranty disclaimer appear in supporting
 * documentation, and that the name of the author not be used in
 * advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.
 *
 * The author disclaim all warranties with regard to this
 * software, including all implied warranties of merchantability
 * and fitness.  In no event shall the author be liable for any
 * special, indirect or consequential damages or any damages
 * whatsoever resulting from loss of use, data or profits, whether
 * in an action of contract, negligence or other tortious action,
 * arising out of or in connection with the use or performance of
 * this software.
 */

#include "whitecat.h"

#include <string.h>
#include <ctype.h>
#include <unistd.h> 
#include <sys/dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <sys/filedesc.h>
#include <sys/uio.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/syslog.h>

#include <sys/spiffs/spiffs.h>
#include <sys/spiffs/spiffs_nucleus.h>
#include <sys/spiffs/esp_spiffs.h>

#if 1 //USE_SPIFFS

#define	EACCESS	5 // Permission denied

#if !defined(max)
#define max(A,B) ( (A) > (B) ? (A):(B))
#endif

#if !defined(min)
#define min(A,B) ( (A) < (B) ? (A):(B))
#endif


spiffs fs;

static u8_t *my_spiffs_work_buf;
static u8_t *my_spiffs_fds;
static u8_t *my_spiffs_cache;


static void dir_path(char *npath, uint8_t base) {
	int len = strlen(npath);

	if (base) {
		char *c;

		c = &npath[len - 1];
		while (c >= npath) {
			if (*c == '/') {
				break;
			}

			len--;
			c--;
		}
	}

    if (len > 1) {
    	if (npath[len - 1] == '.') {
    		npath[len - 1] = '\0';

        	if (npath[len - 2] == '/') {
        		npath[len - 2] = '\0';
        	}
    	} else {
        	if (npath[len - 1] == '/') {
        		npath[len - 1] = '\0';
        	}
    	}
    } else {
    	if ((npath[len - 1] == '/') ||  (npath[len - 1] == '.')) {
    		npath[len - 1] = '\0';
    	}
    }

	if(base != 2) {
		strlcat(npath,"/.", PATH_MAX);
	}
}

static void check_path(const char *path, uint8_t *base_is_dir, uint8_t *full_is_dir, uint8_t *is_file, int *files) {
    char bpath[PATH_MAX + 1]; // Base path
    char fpath[PATH_MAX + 1]; // Full path
    struct spiffs_dirent e;
    spiffs_DIR d;
    int file_num = 0;

    *files = 0;
    *base_is_dir = 0;
    *full_is_dir = 0;
    *is_file = 0;

    // Get base directory name
    strlcpy(bpath, path, PATH_MAX);
    dir_path(bpath, 1);

    // Get full directory name
    strlcpy(fpath, path, PATH_MAX);
    dir_path(fpath, 0);

    SPIFFS_opendir(&fs, "/", &d);
	while (SPIFFS_readdir(&d, &e)) {
		if (!strcmp(bpath, (const char *)e.name)) {
			*base_is_dir = 1;
		}

		if (!strcmp(fpath, (const char *)e.name)) {
			*full_is_dir = 1;
		}

		if (!strcmp(path, (const char *)e.name)) {
			*is_file = 1;
		}

		if (!strncmp(fpath, (const char *)e.name, min(strlen((char *)e.name), strlen(fpath) - 1))) {
			if (strcmp(fpath, (const char *)e.name)) {
				file_num++;
			}
		}
	}
	SPIFFS_closedir(&d);

	*files = file_num;
}

/*
 * This function translate error codes from SPIFFS to errno error codes
 *
static int spiffs_result(int res) {
    switch (res) {
        case SPIFFS_OK:
        case SPIFFS_ERR_END_OF_OBJECT:
            return 0;

        case SPIFFS_ERR_NOT_FOUND:
        case SPIFFS_ERR_CONFLICTING_NAME:
            return ENOENT;

        case SPIFFS_ERR_NOT_WRITABLE:
        case SPIFFS_ERR_NOT_READABLE:
            return EACCES;

        case SPIFFS_ERR_FILE_EXISTS:
            return EEXIST;

        default:
            return res;
    }
}
 */


int spiffs_rmdir_op(const char* name) {
    char npath[PATH_MAX + 1];
//	vfs_spiffs_dir_t *dir = calloc(1, sizeof(vfs_spiffs_dir_t));

//	if (!dir) {
//		errno = ENOMEM;
//		return NULL;
//	}

    // Check path, must be an existing directory
	// files in directory
    uint8_t base_is_dir = 0;
    uint8_t full_is_dir = 0;
    uint8_t is_file = 0;
    int file_num = 0;

    check_path(name, &base_is_dir, &full_is_dir, &is_file, &file_num);
printf("spiffs_rmdir_op0 %s, %d %d %d %d\n", name, base_is_dir, full_is_dir, is_file, file_num);
    if (full_is_dir) {
    	if (file_num > 0) {
    		errno = ENOTEMPTY;
printf("spiffs_rmdir_op1\n");
    		return -1;
    	} else {
    	    strlcpy(npath, name, PATH_MAX);
    	    dir_path(npath, 0);

printf("spiffs_rmdir_op2 %s\n", npath);
	        // Open SPIFFS file
	    	spiffs_file FP = SPIFFS_open(&fs, npath, SPIFFS_RDWR, 0);
	        if (FP < 0) {
	        	errno = spiffs_result(fs.err_code);
printf("spiffs_rmdir_op3 %d\n", errno);
	        	return -1;
	        }

printf("spiffs_rmdir_op4\n");
	        // Remove SPIFSS file
	        if (SPIFFS_fremove(&fs, FP) < 0) {
	            errno = spiffs_result(fs.err_code);
printf("spiffs_rmdir_op5 %d\n", errno);
	        	SPIFFS_close(&fs, FP);
	        	return -1;
	        }

printf("spiffs_rmdir_op6\n");
	    	SPIFFS_close(&fs, FP);
printf("spiffs_rmdir_op7\n");

        	return 0;
    	}
    } else {
       	errno = ENOENT;
		return -1;
    }
}




// Determine if path correspond to a directory entry
int is_dir(const char *path) {
    spiffs_DIR d;
    char npath[MAXPATHLEN];
    int res = 0;
    
    struct spiffs_dirent e;
    
    // Add /. to path
    strcpy(npath, path);
    if (strcmp(path,"/") != 0) {
        strcat(npath,"/.");
    }
    
    SPIFFS_opendir(&fs, "/", &d);   
    while (SPIFFS_readdir(&d, &e)) {
        if (strncmp(npath, (const char *)e.name, strlen(npath)) == 0) {
            res = 1;
            break;
        }
    }	

    SPIFFS_closedir(&d);

    return res;
}

int spiffs_result(int res) {
    switch (res) {
        case SPIFFS_OK:
        case SPIFFS_ERR_END_OF_OBJECT:
            return 0;
        
        case SPIFFS_ERR_NOT_FOUND:
        case SPIFFS_ERR_CONFLICTING_NAME:
            return ENOENT;
            
        case SPIFFS_ERR_NOT_WRITABLE:
        case SPIFFS_ERR_NOT_READABLE:
            return EACCESS;
            
        case SPIFFS_ERR_FILE_EXISTS:
            return EEXIST;
            
        default:
            return res;
    }
}

int spiffs_open_op(struct file *fp, int flags) {
    spiffs_flags mode = 0;
    spiffs_file *FP;
    char *path = fp->f_path;
    int result = 0;
	
    // Calculate open mode
    if (flags == O_RDONLY)
        mode |= SPIFFS_RDONLY;
    
    if (flags & O_WRONLY)
        mode |= SPIFFS_WRONLY;
    
    if (flags & O_RDWR)
        mode = SPIFFS_RDWR;
    
    if (flags & O_EXCL)
        mode |= SPIFFS_EXCL;
    
    if (flags & O_CREAT)
        mode |= SPIFFS_CREAT;

    if (flags & O_TRUNC)
        mode |= SPIFFS_TRUNC;
    
    // Create a FIL structure
    FP = (spiffs_file *)malloc(sizeof(spiffs_file));
    if (!FP) {
        return ENOMEM;
    }
    
    // Store FIL into file
    fp->f_fs = FP;
    fp->f_type = DTYPE_VNODE;
    fp->f_dir = NULL;
    
    if (strcmp(path,"/") == 0) {
        return 0;
    }
    
    if (is_dir(path)) {
        return 0;        
    }
    
    // Open file
    *FP = SPIFFS_open(&fs, path, mode, 0); 
    if (*FP < 0) {
        result = spiffs_result(fs.err_code);
    }
    
    return result;
}

int spiffs_close_op(struct file *fp) {
    int res;
    int result = 0;

    if (fp->f_fs) {    
        res = SPIFFS_close(&fs, *(spiffs_file *)fp->f_fs);
        if (res < 0) {
            result = spiffs_result(fs.err_code);
            if (result == 0) {
                return result;
            }
        }
    }
        
    if ((fp->f_dir) && (result == 0)) {
        result = SPIFFS_closedir((spiffs_DIR *)fp->f_dir);
        if (result < 0) {
            result = spiffs_result(fs.err_code);
        }
    }
    
    return result;
}

int spiffs_read_op(struct file *fp, struct uio *uio, struct ucred *cred) {
    int res;
    char *buf = uio->uio_iov->iov_base;
    unsigned int size = uio->uio_iov->iov_len;

   res = SPIFFS_read(&fs, *(spiffs_file *)fp->f_fs, buf, size); 
    if (res >= 0) {
        uio->uio_resid = uio->uio_resid - res;
        res = 0;
    } else {
        res = spiffs_result(fs.err_code);
    }

    return res;
}

int spiffs_write_op(struct file *fp, struct uio *uio, struct ucred *cred) {
    int res;
    char *buf = uio->uio_iov->iov_base;
    unsigned int size = uio->uio_iov->iov_len;

    res = SPIFFS_write(&fs, *(spiffs_file *)fp->f_fs, buf, size);
    if (res >= 0) {
        uio->uio_resid = uio->uio_resid - res;
        
        res = 0;
    } else {
        res = spiffs_result(fs.err_code);
    }
    
    return res;
}

int spiffs_stat_op(struct file *fp, struct stat *sb) {
    spiffs_stat stat;
    int res;
    
    sb->st_blksize = SPIFFS_LOG_PAGE_SIZE;

    // First test if it's a directory entry
    if (is_dir(fp->f_path)) {
        sb->st_mode = S_IFDIR;
        return 0;
    }

    // If not it's a directory get file statistics
    res = SPIFFS_stat(&fs, fp->f_path, &stat);
    if (res == SPIFFS_OK) {
    	sb->st_size = stat.size;
    } else {
        sb->st_size = 0;
        
        res = spiffs_result(fs.err_code);
    }

    sb->st_mode = S_IFREG;
    
    return res;
}

off_t spiffs_seek_op(struct file *fp, off_t offset, int where) {
    int whence = SPIFFS_SEEK_CUR;
    int res;
    
    switch (where) {
        case SEEK_SET: whence = SPIFFS_SEEK_SET;break;
        case SEEK_CUR: whence = SPIFFS_SEEK_CUR;break;
        case SEEK_END: whence = SPIFFS_SEEK_END;break;
    }
    
    res = SPIFFS_lseek(&fs, *(spiffs_file *)fp->f_fs, offset, whence);
    if (res < 0) {
        res = spiffs_result(fs.err_code);
    }
    
    return res;
}

/*
int spiffs_rename_op(const char *old_filename, const char *new_filename) {  
    int res = SPIFFS_rename(&fs, old_filename, new_filename);
    if (res < 0) {
        res = spiffs_result(fs.err_code);
    }
    
    return res;
}
*/

int spiffs_rename_op(const char *src, const char *dst) {
    char dpath[PATH_MAX + 1];
    char *csrc;
    char *cname;

    // Check paths
    uint8_t src_base_is_dir = 0;
    uint8_t src_full_is_dir = 0;
    uint8_t src_is_file = 0;
    int src_files = 0;

    uint8_t dst_base_is_dir = 0;
    uint8_t dst_full_is_dir = 0;
    uint8_t dst_is_file = 0;
    int dst_files = 0;

    check_path(src, &src_base_is_dir, &src_full_is_dir, &src_is_file, &src_files);
    check_path(dst, &dst_base_is_dir, &dst_full_is_dir, &dst_is_file, &dst_files);

    // Sanity checks
    if (src_is_file && dst_full_is_dir) {
    	errno = EISDIR;
    	return -1;
    }

    if (src_full_is_dir && dst_is_file) {
    	errno = ENOTDIR;
    	return -1;
    }

    if (src_full_is_dir) {
    	// We need to rename all tree
        struct spiffs_dirent e;
        spiffs_DIR d;

//		char npath[PATH_MAX + 1];
//		strlcpy(npath, src, PATH_MAX);
//		dir_path(npath, 2);

    	SPIFFS_opendir(&fs, "/", &d);
    	while (SPIFFS_readdir(&d, &e)) {
printf("rename 1 %s %s\n", src, (const char *)e.name);
//    		if (!strncmp(src, (const char *)e.name, PATH_MAX /*min(strlen((char *)e.name), strlen(src))*/ ) ) {
    		if ( !strncmp(src, (const char *)e.name, strlen(src) ) && e.name[strlen(src)] == '/' ) {
    			strlcpy(dpath, dst, PATH_MAX);
    			csrc = (char *)src;
    			cname = (char *)e.name;

printf("rename 2 %s\n", (const char *)e.name);
    			while (*csrc && *cname && (*csrc == *cname)) {
    				++csrc;
    				++cname;
    			}

printf("rename 3 %s\n", cname);
				strlcat(dpath, cname, PATH_MAX);
printf("rename 4 %s\n", dpath);

    	    	if (SPIFFS_rename(&fs, (char *)e.name, dpath) < 0) {
    	        	errno = spiffs_result(fs.err_code);
    	        	return -1;
    	        }
    		}
    	}
    	SPIFFS_closedir(&d);
    } else {
    	if (SPIFFS_rename(&fs, src, dst) < 0) {
        	errno = spiffs_result(fs.err_code);
        	return -1;
        }
    }

	return 0;
}








int spiffs_unlink_op(struct file *fp) {
    spiffs_file fh = *(spiffs_file *)fp->f_fs;
    spiffs_fd *fd;
    s32_t res;

    res = spiffs_fd_get(&fs, fh, &fd);
    if (res == SPIFFS_ERR_BAD_DESCRIPTOR) {
        char npath[MAXPATHLEN];

        strcpy(npath, fp->f_path);

        if (is_dir(fp->f_path)) {
            if (strcmp(fp->f_path,"/") != 0) {
                strcat(npath,"/.");
            }            
        }

        fh = SPIFFS_open(&fs, npath, SPIFFS_RDWR, 0); 
    }
    
    res = SPIFFS_fremove(&fs, fh);
    if (res < 0) {
        res = spiffs_result(fs.err_code);
    }
    
    return res;
}
    
int spiffs_opendir_op(struct file *fp) {
    fp->f_dir = malloc(sizeof(spiffs_DIR));
    if (!fp->f_dir) {
        return ENOMEM;
    }
    
    if (!SPIFFS_opendir(&fs, fp->f_path, fp->f_dir)) {
        free(fp->f_dir);
        
        return spiffs_result(fs.err_code);
    }

    return 0;
}

int spiffs_mkdir_op(const char *path) {
    char npath[MAXPATHLEN];

    if (is_dir(path)) {
        return EEXIST;
    }

    // Add /. to path
    strcpy(npath, path);
    if ((strcmp(path,"/") != 0) && (strcmp(path,"/.") != 0)) {
        strcat(npath,"/.");
    }
    
    spiffs_file fd = SPIFFS_open(&fs, npath, SPIFFS_CREAT, 0);
    if (fd < 0) {
        return spiffs_result(fs.err_code);
    }
    
    if (SPIFFS_close(&fs, fd) < 0) {
        return spiffs_result(fs.err_code);
    }
    
    return 0;
}

int spiffs_readdir_op(struct file *fp, struct dirent *ent) {
    struct spiffs_dirent e;
    struct spiffs_dirent *pe = &e;
    char *fn;
    int res = 0;
    int len = 0;
    
    *ent->d_name = '\0';
    for(;;) {
        // Read directory        
        pe = SPIFFS_readdir((spiffs_DIR *)fp->f_dir, pe);
        if (!pe) {
            res = spiffs_result(fs.err_code);
            break;
        }

        // Break condition
        if (pe->name[0] == 0) break;            
                
        // Get name and length
        fn = (char *)pe->name;
        len = strlen(fn);

        // Get entry type and size
        ent->d_type = DT_REG;
        ent->d_reclen = pe->size;

        if (len >= 2) {
            if (fn[len - 1] == '.') {
                if (fn[len - 2] == '/') {
                    ent->d_type = DT_DIR;
                    ent->d_reclen = 0;
                    
                    fn[len - 2] = '\0';
                    
                    len = strlen(fn);
                    
                    // Skip root dir
                    if (len == 0) {
                        continue;
                    }
                }
            }
        }
        
        // Skip entries not belonged to path
        if (strncmp(fn, fp->f_path, strlen(fp->f_path)) != 0) {
            continue;
        }

        if (strlen(fp->f_path) > 1) {
            if (*(fn + strlen(fp->f_path)) != '/') {
                continue;
            }
        }
 
        // Skip root directory
        fn = fn + strlen(fp->f_path);
        len = strlen(fn);
        if (len == 0) {
            continue;
        }
                
        // Skip initial /
        if (len > 1) { 
            if (*fn == '/') {
                fn = fn + 1;
                len--;
            }
        }
        
        // Skip subdirectories
        if (strchr(fn,'/')) {
            continue;
        }
        
        ent->d_namlen = len;

        strcpy(ent->d_name, fn);

        break;
    }
    
    return res;
}

/*
void spiffs_copy_image(const char *path) {
    DIR *dir = NULL;
    struct dirent *ent;
    char npaths[MAXPATHLEN];
    char npathd[MAXPATHLEN];
    FILE *fsrc, *fdst;
    char c;
    
    dir = opendir(path);
    if (dir) {
        while ((ent = readdir(dir)) != NULL) {
            strcpy(npaths, path);
            strcat(npaths,"/");
            strcat(npaths,ent->d_name);

            if (ent->d_type != DT_DIR) {
                strcpy(npathd, path + 13);
                strcat(npathd,"/");
                strcat(npathd,ent->d_name);
                
                syslog(LOG_INFO, "spiffs0 copy %s to %s", npaths, npathd);
                
                fsrc = fopen(npaths,"r");
                fdst = fopen(npathd,"w");
                
                c = fgetc(fsrc);
                while (!feof(fsrc)) {
                    fputc(c, fdst);
                    c = fgetc(fsrc);    
                }
                
                fclose(fsrc);
                fclose(fdst);                
            } else {
                spiffs_copy_image(npaths);
            }
        }
        
        closedir(dir);
    }
}
*/

int spiffs_format() {    
    SPIFFS_unmount(&fs);
    SPIFFS_format(&fs);
    
    //cpu_reset();
	
	return 0;
}

int spiffs_mount() {
    spiffs_config cfg;
    int unit = 0;
    int res = 0;
    int retries = 0;

    cfg.phys_addr 		 = SPIFFS_BASE_ADDR;
    cfg.phys_size 		 = SPIFFS_SIZE;
    cfg.phys_erase_block = SPIFFS_ERASE_SIZE;
    cfg.log_page_size    = SPIFFS_LOG_PAGE_SIZE;
    cfg.log_block_size   = SPIFFS_LOG_BLOCK_SIZE;
    
    syslog(LOG_INFO, "spiffs%d start address at 0x%x, size %d Kb",
           unit, cfg.phys_addr, cfg.phys_size / 1024
    );
    
	cfg.hal_read_f = esp_spiffs_read;
	cfg.hal_write_f = esp_spiffs_write;
	cfg.hal_erase_f = esp_spiffs_erase;
    
    my_spiffs_work_buf = malloc(cfg.log_page_size * 2);
    if (!my_spiffs_work_buf) {
        errno = ENOMEM;
        return -1;
    }
    
    int fds_len = sizeof(spiffs_fd) * 5;
    my_spiffs_fds = malloc(fds_len);
    if (!my_spiffs_fds) {
        free(my_spiffs_work_buf);
        errno = ENOMEM;
        return -1;                
    }
    
    int cache_len = cfg.log_page_size * 5;
    my_spiffs_cache = malloc(cache_len);
    if (!my_spiffs_cache) {
        free(my_spiffs_work_buf);
        free(my_spiffs_fds);
        errno = ENOMEM;
        return -1;        
    }
    
    retries = 0;
    
retry:
    if (retries > 2) {
        syslog(LOG_ERR, "spiffs%d can't mount file", unit);
        return -1;
    }

    res = SPIFFS_mount(
            &fs, &cfg, my_spiffs_work_buf, my_spiffs_fds,
            fds_len, my_spiffs_cache, cache_len,NULL
    );

    if (res < 0) {
        if (fs.err_code == SPIFFS_ERR_NOT_A_FS) {
            syslog(LOG_ERR, "spiffs%d no file system detect, formating", unit);
            SPIFFS_unmount(&fs);
            res = SPIFFS_format(&fs);
            if (res < 0) {
                syslog(LOG_ERR, "spiffs%d format error",unit);
                return -1;
            }
                        
            retries++;
            goto retry;
        }
    } else {
        if (retries > 0) {
            spiffs_mkdir_op("/.");
        }
    }
    
    syslog(LOG_INFO, "spiffs%d mounted", unit);

    return 1;
}

int spiffs_init() {
    return spiffs_mount();
}
#endif
