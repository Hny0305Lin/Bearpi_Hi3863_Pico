/* ----------------------------------------------------------------------------
 * Copyright (c) Huawei Technologies Co., Ltd. 2013-2019. All rights reserved.
 * Description: LiteOS Disk Module Headfile
 * Author: Huawei LiteOS Team
 * Create: 2013-01-01
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 * of conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * --------------------------------------------------------------------------- */

/**
 * @defgroup disk Disk
 * @ingroup filesystem
 */

#ifndef _DISK_H
#define _DISK_H

#include "los_base.h"
#include "inode/inode.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define SYS_MAX_DISK                5
#define MAX_DIVIDE_PART_PER_DISK    16
#define MAX_PRIMARY_PART_PER_DISK   4
#define SYS_MAX_PART                (SYS_MAX_DISK * MAX_DIVIDE_PART_PER_DISK)
#define DISK_NAME                   255
#define EMMC                 0xEC

/* Command code for disk_ioctrl function */
/* Generic command (Used by FatFs) */
#define DISK_CTRL_SYNC          0   /* Complete pending write process */
#define DISK_GET_SECTOR_COUNT   1   /* Get media size */
#define DISK_GET_SECTOR_SIZE    2   /* Get sector size */
#define DISK_GET_BLOCK_SIZE     3   /* Get erase block size */
#define DISK_CTRL_TRIM          4   /* Inform device that the data on the block of sectors is no longer used */

/* Generic command (Not used by FatFs) */
#define DISK_CTRL_POWER         5   /* Get/Set power status */
#define DISK_CTRL_LOCK          6   /* Lock/Unlock media removal */
#define DISK_CTRL_EJECT         7   /* Eject media */
#define DISK_CTRL_FORMAT        8   /* Create physical format on the media */

/* MMC/SDC specific ioctl command */
#define DISK_MMC_GET_TYPE       10  /* Get card type */
#define DISK_MMC_GET_CSD        11  /* Get CSD */
#define DISK_MMC_GET_CID        12  /* Get CID */
#define DISK_MMC_GET_OCR        13  /* Get OCR */
#define DISK_MMC_GET_SDSTAT     14  /* Get SD status */

/* ATA/CF specific ioctl command */
#define DISK_ATA_GET_REV        20  /* Get F/W revision */
#define DISK_ATA_GET_MODEL      21  /* Get model name */
#define DISK_ATA_GET_SN         22  /* Get serial number */

#define DISK_CTRL_TRIM_REVERT   23    /* Inform device that the data on the block of sectors will be used */

typedef struct _los_disk_ {
    UINT32 disk_id : 8;     /* physics disk number */
    UINT32 disk_status : 2; /* status of disk */
    UINT32 part_count : 8;  /* current partition count */
    UINT32 reserved : 14;
    struct inode *dev;      /* device */
#ifdef LOSCFG_FS_FAT_CACHE
    VOID *bcache;           /* cache of the disk, shared in all partitions */
#endif
    UINT32 sector_size;     /* disk sector size */
    UINT64 sector_start;    /* disk start sector */
    UINT64 sector_count;    /* disk sector number */
    UINT8  type;
    CHAR disk_name[DISK_NAME + 1];
    LOS_DL_LIST head;       /* link head of all the partitions */
    UINT32 disk_mutex;
} los_disk;

typedef struct _los_part_ {
    UINT32 disk_id : 8;      /* physics disk number */
    UINT32 part_id : 8;      /* partition number in the system */
    UINT32 part_no_disk : 8; /* partition number in the disk */
    UINT32 part_no_mbr : 5;  /* partition number in the mbr */
    UINT32 reserved : 3;
    UINT8 filesystem_type;   /* filesystem used in the partition */
    struct inode  *dev;      /* dev devices used in the partition */
    UINT64 sector_start;     /*
                              * offset of a partition to the primary devices
                              * (multi-mbr partitions are seen as same parition)
                              */
    UINT64 sector_count;     /*
                              * sector numbers of a partition. If there is no addpartition operation,
                              * then all the mbr devices equal to the primary device count.
                              */
    UINT8 type;
    CHAR *part_name;
    LOS_DL_LIST list;        /* linklist of partition */

#ifdef LOSCFG_FS_FAT_UNI_TRIM
    VOID *trimManager;
#endif
} los_part;

struct partition_info {
    UINT8  type;
    UINT64 sector_start;
    UINT64 sector_count;
};

struct disk_divide_info {
    UINT64 sector_count;
    UINT32 sector_size;
    UINT32 part_count;
    /*
     * The primary partition place should be reversed and set to 0 in case all the partitions are
     * logical partition (maximum 16 currently). So the maximum part number should be 4 + 16.
     */
    struct partition_info part[MAX_DIVIDE_PART_PER_DISK + MAX_PRIMARY_PART_PER_DISK];
};

/**
 * @ingroup  disk
 * @brief Disk driver initialization.
 *
 * @par Description:
 * Initializate a disk dirver, and set the block cache.
 *
 * @attention
 * <ul>
 * <li>The parameter diskName must point a valid string, which end with the terminating null byte.</li>
 * <li>The total length of parameter diskName must be less than the value defined by PATH_MAX.</li>
 * <li>The parameter bops must pointed the right functions, otherwise the system
 * will crash when the disk is being operated.</li>
 * <li>The parameter info can be null or point to struct disk_divide_info. when info is null,
 * the disk will be divided base the information of MBR, otherwise,
 * the disk will be divided base the information of parameter info.</li>
 * </ul>
 *
 * @param  diskName  [IN] Type #const CHAR *                      disk driver name.
 * @param  bops      [IN] Type #const struct block_operations *   block driver control structure.
 * @param  priv      [IN] Type #VOID *                            private data of inode.
 * @param  diskId    [IN] Type #INT32                             disk id number, less than SYS_MAX_DISK.
 * @param  info      [IN] Type #VOID *                            disk driver partition information.
 *
 * @retval #0      Initialization success.
 * @retval #-1     Initialization failed.
 *
 * @par Dependency:
 * <ul><li>disk.h</li></ul>
 * @see los_disk_deinit
 *
 * @since Huawei LiteOS V100R001C00
 */
INT32 los_disk_init(const CHAR *diskName, const struct block_operations *bops,
                    VOID *priv, INT32 diskId, VOID *info);

/**
 * @ingroup  disk
 * @brief Destroy a disk driver.
 *
 * @par Description:
 * Destroy a disk driver, free the dependent resource.
 *
 * @attention
 * <ul>
 * None
 * </ul>
 *
 * @param  diskId [IN] Type #INT32  disk driver id number, less than the value defined by SYS_MAX_DISK.
 *
 * @retval #0      Destroy success.
 * @retval #-1     Destroy failed.
 *
 * @par Dependency:
 * <ul><li>disk.h</li></ul>
 * @see los_disk_init
 *
 * @since Huawei LiteOS V100R001C00
 */
INT32 los_disk_deinit(INT32 diskId);

/**
 * @ingroup  disk
 * @brief Read data from disk driver.
 *
 * @par Description:
 * Read data from disk driver.
 *
 * @attention
 * <ul>
 * <li>The sector size of the disk to be read should be acquired by los_part_ioctl before calling this function.</li>
 * <li>The parameter buf must point to a valid memory and the buf size is count * sector_size.</li>
 * </ul>
 *
 * @param  drvId   [IN]  Type #INT32         disk driver id number, less than the value defined by SYS_MAX_DISK.
 * @param  buf     [OUT] Type #VOID *        memory which used to store read data.
 * @param  sector  [IN]  Type #UINT64        expected start sector number to read.
 * @param  count   [IN]  Type #UINT32        expected sector count to read.
 *
 * @retval #0      Read success.
 * @retval #-1     Read failed.
 *
 * @par Dependency:
 * <ul><li>disk.h</li></ul>
 * @see los_disk_write
 *
 * @since Huawei LiteOS V100R001C00
 */
INT32 los_disk_read(INT32 drvId, VOID *buf, UINT64 sector, UINT32 count);

/**
 * @ingroup  disk
 * @brief Write data to a disk driver.
 *
 * @par Description:
 * Write data to a disk driver.
 *
 * @attention
 * <ul>
 * <li>The sector size of the disk to be read should be acquired by los_part_ioctl before calling this function.</li>
 * <li>The parameter buf must point to a valid memory and the buf size is count * sector_size.</li>
 * </ul>
 *
 * @param  drvId   [IN]  Type #INT32           disk driver id number, less than the value defined by SYS_MAX_DISK.
 * @param  buf     [IN]  Type #const VOID *    memory which used to storage write data.
 * @param  sector  [IN]  Type #UINT64          expected start sector number to read.
 * @param  count   [IN]  Type #UINT32          experted sector count of write.
 *
 * @retval #0      Write success.
 * @retval #-1     Write failed.
 *
 * @par Dependency:
 * <ul><li>disk.h</li></ul>
 * @see los_disk_read
 *
 * @since Huawei LiteOS V100R001C00
 */
INT32 los_disk_write(INT32 drvId, const VOID *buf, UINT64 sector, UINT32 count);

/**
 * @ingroup  disk
 * @brief Get information of disk driver.
 *
 * @par Description:
 * Get information of disk driver.
 *
 * @attention
 * <ul>
 * None
 * </ul>
 *
 * @param  drvId [IN]  Type #INT32     disk driver id number, less than the value defined by SYS_MAX_DISK.
 * @param  cmd   [IN]  Type #INT32     command to issu, currently support DISK_GET_SECTOR_COUNT, DISK_GET_SECTOR_SIZE,
 *                                     DISK_GET_BLOCK_SIZE, DISK_CTRL_SYNC.
 * @param  buf   [OUT] Type #VOID *    memory to storage the information, different cmd require different buf sizes:
 *                                     DISK_CTRL_SYNC(NULL)
 *                                     DISK_GET_SECTOR_COUNT(UINT64)
 *                                     DISK_GET_SECTOR_SIZE(size_t)
 *                                     DISK_GET_BLOCK_SIZE(size_t)
 *
 * @retval #0      Get information success.
 * @retval #-1     Get information failed.
 *
 * @par Dependency:
 * <ul><li>disk.h</li></ul>
 * @see None
 *
 * @since Huawei LiteOS V100R001C00
 */
INT32 los_disk_ioctl(INT32 drvId, INT32 cmd, VOID *buf);

/**
 * @ingroup  disk
 * @brief Sync blib cache.
 *
 * @par Description:
 * Sync blib cache, write the valid data to disk driver.
 *
 * @attention
 * <ul>
 * None
 * </ul>
 *
 * @param  drvId [IN] Type #INT32  disk driver id number, less than the value defined by SYS_MAX_DISK.
 *
 * @retval #0         Sync success.
 * @retval #INT32     Sync failed.
 *
 * @par Dependency:
 * <ul><li>disk.h</li></ul>
 * @see None
 *
 * @since Huawei LiteOS V100R001C00
 */
INT32 los_disk_sync(INT32 drvId);

/**
 * @ingroup  disk
 * @brief Set blib cache for the disk driver.
 *
 * @par Description:
 * Set blib cache for the disk driver, users can set the number of sectors of per block,
 * and the number of blocks.
 *
 * @attention
 * <ul>
 * None
 * </ul>
 *
 * @param  drvId             [IN] Type #INT32     disk driver id number, less than the value defined by SYS_MAX_DISK.
 * @param  sectorPerBlock    [IN] Type #UINT32    sector number of per block, only can be 32 * (1, 2, ..., 8).
 * @param  blockNum          [IN] Type #UINT32    block number of cache.
 *
 * @retval #0         Set success.
 * @retval #INT32     Set failed.
 *
 * @par Dependency:
 * <ul><li>disk.h</li></ul>
 * @see None
 *
 * @since Huawei LiteOS V100R001C00
 */
INT32 los_disk_set_bcache(INT32 drvId, UINT32 sectorPerBlock, UINT32 blockNum);

/**
 * @ingroup  disk
 * @brief Read data from chosen partition.
 *
 * @par Description:
 * Read data from chosen partition.
 *
 * @attention
 * <ul>
 * <li>The sector size of the disk to be read should be acquired by los_part_ioctl before calling this function.</li>
 * <li>The parameter buf must point to valid memory and the buf size is count * sector_size.</li>
 * </ul>
 *
 * @param  pt      [IN]  Type #INT32        partition number, less than the value defined by SYS_MAX_PART.
 * @param  buf     [OUT] Type #VOID *       memory which used to store the data to be read.
 * @param  sector  [IN]  Type #UINT64       start sector number of chosen partition.
 * @param  count   [IN]  Type #UINT32       the expected sector count for reading.
 *
 * @retval #0      Read success.
 * @retval #-1     Read failed.
 *
 * @par Dependency:
 * <ul><li>disk.h</li></ul>
 * @see los_part_read
 *
 * @since Huawei LiteOS V100R001C00
 */
INT32 los_part_read(INT32 pt, VOID *buf, UINT64 sector, UINT32 count);

/**
 * @ingroup  disk
 * @brief Write data to chosen partition.
 *
 * @par Description:
 * Write data to chosen partition.
 *
 * @attention
 * <ul>
 * <li>The sector size of the disk to be write should be acquired by los_part_ioctl before calling this function.</li>
 * <li>The parameter buf must point to valid memory and the buf size is count * sector_size.</li>
 * </ul>
 *
 * @param  pt      [IN] Type #INT32        partition number,less than the value defined by SYS_MAX_PART.
 * @param  buf     [IN] Type #VOID *       memory which used to storage the written data.
 * @param  sector  [IN] Type #UINT64       start sector number of chosen partition.
 * @param  count   [IN] Type #UINT32       the expected sector count for write.
 *
 * @retval #0      Write success.
 * @retval #-1     Write failed.
 *
 * @par Dependency:
 * <ul><li>disk.h</li></ul>
 * @see los_part_read
 *
 * @since Huawei LiteOS V100R001C00
 */
INT32 los_part_write(INT32 pt, const VOID *buf, UINT64 sector, UINT32 count);

/**
 * @ingroup  disk
 * @brief Get information of chosen partition.
 *
 * @par Description:
 * By passed command to get information of chosen partition.
 *
 * @attention
 * <ul>
 * None
 * </ul>
 *
 * @param  pt   [IN]  Type #INT32      partition number,less than the value defined by SYS_MAX_PART.
 * @param  cmd  [IN]  Type #INT32      command to issu, currently support GET_SECTOR_COUNT, GET_SECTOR_SIZE,
 *                                     GET_BLOCK_SIZE, CTRL_SYNC, TRIM_CMD.
 * @param  buf  [OUT] Type #VOID *     memory to store the information, different cmd require different buf size:
 *                                     CTRL_SYNC(NULL)
 *                                     GET_SECTOR_COUNT(UINT64)
 *                                     GET_SECTOR_SIZE(size_t)
 *                                     GET_BLOCK_SIZE(size_t)
 *                                     TRIM_CMD(UINT64 * 2).
 *
 * @retval #0      Get information success.
 * @retval #-1     Get information failed.
 *
 * @par Dependency:
 * <ul><li>disk.h</li></ul>
 * @see None
 *
 * @since Huawei LiteOS V100R001C00
 */
INT32 los_part_ioctl(INT32 pt, INT32 cmd, VOID *buf);

/**
 * @ingroup  disk
 * @brief Decide the chosen partition is exist or not.
 *
 * @par Description:
 * Decide the chosen partition is exist or not.
 *
 * @attention
 * <ul>
 * <li>The parameter dev is a full path, which begin with '/' and end with '/0'.</li>
 * </ul>
 *
 * @param  dev  [IN]  Type #const CHAR *    partition driver name.
 * @param  mode [IN]  Type #mode_t          access modd.
 *
 * @retval #0      The chosen partition is exist.
 * @retval #-1     The chosen partition is not exist.
 *
 * @par Dependency:
 * <ul><li>disk.h</li></ul>
 * @see None
 *
 * @since Huawei LiteOS V100R001C00
 */
INT32 los_part_access(const CHAR *dev, mode_t mode);

/**
 * @ingroup  disk
 * @brief Find disk partition.
 *
 * @par Description:
 * By driver partition inode to find disk partition.
 *
 * @attention
 * <ul>
 * None
 * </ul>
 *
 * @param  blkDriver  [IN]  Type #struct inode *    partition driver inode.
 *
 * @retval #NULL           Can't find chosen disk partition.
 * @retval #los_part *     This is partition structure pointer of chosen disk partition.
 *
 * @par Dependency:
 * <ul><li>disk.h</li></ul>
 * @see None
 *
 * @since Huawei LiteOS V100R001C00
 */
los_part *los_part_find(const struct inode *blkDriver);

/**
 * @ingroup  disk
 * @brief Find disk driver.
 *
 * @par Description:
 * By disk driver id number to find disk dirver.
 *
 * @attention
 * <ul>
 * None
 * </ul>
 *
 * @param  id  [IN]  Type #INT32  disk id number,less than the value defined by SYS_MAX_DISK.
 *
 * @retval #NULL           Can't find chosen disk driver.
 * @retval #los_disk *     This is disk structure pointer of chosen disk driver.
 *
 * @par Dependency:
 * <ul><li>disk.h</li></ul>
 * @see None
 *
 * @since Huawei LiteOS V100R001C00
 */
los_disk *get_disk(INT32 id);

/**
 * @ingroup  disk
 * @brief Find disk partition.
 *
 * @par Description:
 * By driver partition id number to find disk partition.
 *
 * @attention
 * <ul>
 * None
 * </ul>
 *
 * @param  id  [IN]  Type #INT32 partition id number,less than the value defined by SYS_MAX_PART.
 *
 * @retval #NULL           Can't find chosen disk partition.
 * @retval #los_part *     This is partition structure pointer of chosen disk partition.
 *
 * @par Dependency:
 * <ul><li>disk.h</li></ul>
 * @see None
 *
 * @since Huawei LiteOS V100R001C00
 */
los_part *get_part(INT32 id);

/**
 * @ingroup  disk
 * @brief Print partition information.
 *
 * @par Description:
 * Print partition information.
 *
 * @attention
 * <ul>
 * None
 * </ul>
 *
 * @param  part  [IN]  Type #los_part *  partition control structure pointer
 *
 * @par Dependency:
 * <ul><li>disk.h</li></ul>
 * @see None
 *
 * @since Huawei LiteOS V100R001C00
 */
VOID show_part(const los_part *part);

/**
 * @ingroup  disk
 * @brief Add a new mmc partition.
 *
 * @par Description:
 * Add a new mmc partition, users can set the start sector and size of the new partition.
 *
 * @attention
 * <ul>
 * None
 * </ul>
 *
 * @param  info          [IN]  Type #struct disk_divide_info *  Disk driver information structure pointer.
 * @param  sectorStart   [IN]  Type #size_t                     Start sector number of the new partition.
 * @param  sectorCount   [IN]  Type #size_t                     Sector count of the new partition.
 *
 * @retval #0      Add partition success.
 * @retval #-1     Add partition failed.
 *
 * @par Dependency:
 * <ul><li>disk.h</li></ul>
 * @see None
 *
 * @since Huawei LiteOS V100R001C00
 */
INT32 add_mmc_partition(struct disk_divide_info *info, size_t sectorStart, size_t sectorCount);

/**
 * @ingroup  disk
 * @brief alloc a new UNUSED disk id.
 *
 * @par Description:
 * Get a free disk id for new device.
 *
 * @attention
 * <ul>
 * <li>The parameter diskName must point a valid string, which end with the null byte ('\0') </li>
 * <li>The total length of parameter diskName must be less than the value defined by DISK_NAME </li>
 * </ul>
 *
 * @param  diskName      [IN]  Type #const CHAR *   device name.
 *
 * @retval #INT32        available disk id
 * @retval #-1           alloc disk id failed

 * @par Dependency:
 * <ul><li>disk.h</li></ul>
 * @see los_get_diskid_byname
 *
 * @since Huawei LiteOS V200R001C00
 */
INT32 los_alloc_diskid_byname(const CHAR *diskName);

/**
 * @ingroup  disk
 * @brief get the disk id in used.
 *
 * @par Description:
 * Get the disk id in used by diskName.
 *
 * @attention
 * <ul>
 * <li>The parameter diskName must point a valid string, which end with the null byte ('\0') </li>
 * <li>The total length of parameter diskName must be less than the value defined by DISK_NAME </li>
 * </ul>
 *
 * @param  diskName      [IN]  Type #const CHAR *  device name.
 *
 * @retval #INT32       available disk id
 * @retval #-1          get disk id failed

 * @par Dependency:
 * <ul><li>disk.h</li></ul>
 * @see los_alloc_diskid_byname
 *
 * @since Huawei LiteOS V200R001C00
 */
INT32 los_get_diskid_byname(const CHAR *diskName);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
