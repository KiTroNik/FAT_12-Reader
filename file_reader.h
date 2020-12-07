//
// Created by Tomala on 03.12.2020.
//

#ifndef PROJECT1_FILE_READER_H
#define PROJECT1_FILE_READER_H

#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define SUCCESS 0
#define NOMEM 1
#define CORRUPTED 2
#define DISK_READ_FAULT 3

typedef uint32_t lba_t; // sektory
typedef uint32_t cluster_t; // klastry

struct my_time_t {
    uint16_t second;
    uint16_t minute;
    uint16_t hour;
} __attribute__ (( packed ));

struct my_date_t {
    uint16_t year;
    uint16_t month;
    uint16_t day;
} __attribute__ (( packed ));

struct disk_t {
    FILE *disk;
    uint16_t size_of_block;
    uint16_t num_of_blocks;
};

struct disk_t* disk_open_from_file(const char* volume_file_name);
uint16_t calc_num_of_blocks (struct disk_t *d);
int disk_read(struct disk_t* pdisk, int32_t first_sector, void* buffer, int32_t sectors_to_read);
int disk_close(struct disk_t* pdisk);

struct fat_super_t {
    uint8_t jump_code[3];
    char oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint16_t root_dir_capacity;
    uint16_t logical_sectors16;
    uint8_t media_type;
    uint16_t sectors_per_fat;
    uint16_t chs_sectors_per_track;
    uint16_t chs_tracks_per_cylinder;
    uint32_t hidden_sectors;
    uint32_t logical_sectors32;
    uint8_t media_id;
    uint8_t chs_head;
    uint8_t ext_bpb_signature;
    uint32_t serial_number;
    char    volume_label[11];
    char    fsid[8];
    uint8_t boot_code[448];
    uint16_t magic;
} __attribute__ (( packed ));

enum fat_attributes_t {
    FAT_ATTRIB_READONLY = 0x01,
    FAT_ATTRIB_HIDDEN = 0x02,
    FAT_ATTRIB_SYSTEM = 0x04,
    FAT_ATTRIB_LABEL = 0x08,
    FAT_ATTRIB_DIR = 0x10,
    FAT_ATTRIB_ARCHIVED = 0x20,
    FAT_ATTRIB_LFN = 0x0F
} __attribute__ (( packed ));

struct fat_sfn_t {
    uint8_t file_name[8 + 3];
    enum fat_attributes_t file_attribute;
    uint8_t reserved;
    uint8_t creation_time_ms;
    uint16_t file_creation_time;
    uint16_t file_creation_date;
    uint16_t file_access_date;
    uint16_t file_first_high;
    uint16_t file_modified_time;
    uint16_t file_modified_date;
    uint16_t file_first_low;
    uint32_t file_size;
} __attribute__ (( packed ));

struct volume_geometry {
    lba_t volume_start;
    lba_t fat_1_position;
    lba_t fat_2_position;
    lba_t rootdir_position;
    lba_t rootdir_size;
    lba_t cluster2_position;
    lba_t volume_size;
    lba_t user_space;
    cluster_t total_clusters;
};

struct volume_t {
    struct fat_super_t super_sector;
    struct volume_geometry geometry;
    uint8_t *fat_1;
    uint8_t *fat_2;
    struct fat_sfn_t *root_directory;
    uint8_t *data_area;
    uint16_t *fat_data;
} __attribute__ (( packed ));

struct volume_t* fat_open (struct disk_t* pdisk, uint32_t first_sector);
void handle_errno (int err_code, struct volume_t *vol);
int read_super_sector (struct disk_t *pdisk, struct volume_t * volume);
void calculate_volume_geometry (struct volume_t *volume);
int validate_super_sector (struct fat_super_t super);
int read_fats (struct disk_t *pdisk, struct volume_t * volume);
int read_root_dir (struct disk_t *pdisk, struct volume_t * volume);
int read_data_area (struct disk_t *pdisk, struct volume_t * volume);
int read_fat_data (struct volume_t *volume);
int fat_close (struct volume_t* pvolume);

struct file_t{
    uint8_t *data;
    int curr_position;
    int size;
};

struct file_t* file_open (struct volume_t* pvolume, const char* file_name);
struct fat_sfn_t * search_for_file (struct volume_t* pvolume, const char* file_name);
char *make_name (const uint8_t *file_name);
cluster_t get_next_cluster (struct volume_t *volume, cluster_t current);
int file_close (struct file_t* stream);
size_t file_read (void *ptr, size_t size, size_t nmemb, struct file_t *stream);
int32_t file_seek (struct file_t* stream, int32_t offset, int whence);

struct dir_entry_t {
    char name[13];
    uint32_t size;
    uint8_t is_archived;
    uint8_t is_readonly;
    uint8_t is_system;
    uint8_t is_hidden;
    uint8_t is_directory;
    struct my_date_t creation_date;
    struct my_time_t creation_time;
    cluster_t cluster; // koło 51 minuty
};

struct dir_t {
    struct dir_entry_t *content;
    int current;
    int num_of_elements;
};

struct dir_t* dir_open (struct volume_t* pvolume, const char* dir_path);
void fill_dir_entry(struct dir_entry_t *entry, const struct fat_sfn_t *sfn);
void fill_attributes(struct dir_entry_t *entry, const struct fat_sfn_t *sfn);
void fill_date(struct dir_entry_t *entry, const struct fat_sfn_t *sfn);
void fill_time (struct dir_entry_t *entry, const struct fat_sfn_t *sfn);
int extract_bits(int number, int k, int p);
void clear_attributes (struct dir_entry_t *entry);
void fill_name(struct dir_entry_t *entry, const struct fat_sfn_t *sfn);
int dir_close (struct dir_t* pdir);
int dir_read (struct dir_t* pdir, struct dir_entry_t* pentry);

#endif //PROJECT1_FILE_READER_H
