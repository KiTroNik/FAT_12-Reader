//
// Created by Tomala on 03.12.2020.
//

#include "file_reader.h"

struct disk_t* disk_open_from_file (const char* volume_file_name) {
    if (volume_file_name == NULL) {
        errno = EFAULT;
        return NULL;
    }

    struct disk_t *result = malloc (sizeof(struct disk_t));
    if (result == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    result->size_of_block = 512;
    result->disk = fopen(volume_file_name, "rb");
    if (result->disk == NULL) {
        errno = ENOENT;
        free (result);
        return NULL;
    }
    result->num_of_blocks = calc_num_of_blocks (result);

    return result;
}

uint16_t calc_num_of_blocks (struct disk_t *d) {
    uint16_t result = 0;
    char *buff = malloc(d->size_of_block);
    while(!feof(d->disk)) {
        fread(buff, d->size_of_block, 1, d->disk);
        result++;
    }
    fseek(d->disk, 0, SEEK_SET);
    free (buff);
    return result;
}

int disk_read (struct disk_t* pdisk, int32_t first_sector, void* buffer, int32_t sectors_to_read) {
    if (pdisk == NULL || buffer == NULL || sectors_to_read <= 0 || first_sector < 0) {
        errno = EFAULT;
        return -1;
    }

    if (pdisk->num_of_blocks - first_sector < sectors_to_read) {
        errno = ERANGE;
        return -1;
    }

    fseek (pdisk->disk, first_sector * pdisk->size_of_block, SEEK_SET);
    int result = (int)fread (buffer, pdisk->size_of_block, sectors_to_read, pdisk->disk);

    fseek(pdisk->disk, 0, SEEK_SET);
    return result;
}

int disk_close(struct disk_t* pdisk) {
    if (pdisk == NULL || pdisk->disk == NULL) {
        errno = EFAULT;
        return -1;
    }

    fclose(pdisk->disk);
    free(pdisk);
    return 0;
}

struct volume_t* fat_open(struct disk_t* pdisk, uint32_t first_sector) {
    if (pdisk == NULL) {
        errno = EFAULT;
        return NULL;
    }

    struct volume_t *volume = malloc(sizeof(struct volume_t));
    if (volume == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    volume->fat_1 = NULL;
    volume->fat_2 = NULL;
    volume->root_directory = NULL;
    volume->data_area = NULL;
    volume->fat_data = NULL;

    int err_code = read_super_sector(pdisk, volume);
    if (err_code != SUCCESS) {
        handle_errno(err_code, volume);
        return NULL;
    }

    err_code = validate_super_sector(volume->super_sector);
    if (err_code != SUCCESS) {
        handle_errno(err_code, volume);
        return NULL;
    }

    calculate_volume_geometry(volume);

    err_code = read_fats(pdisk, volume);
    if (err_code != SUCCESS) {
        handle_errno(err_code, volume);
        return NULL;
    }

    err_code = read_root_dir (pdisk, volume);
    if (err_code != SUCCESS) {
        handle_errno(err_code, volume);
        return NULL;
    }

    err_code = read_data_area (pdisk, volume);
    if (err_code != SUCCESS) {
        handle_errno(err_code, volume);
        return NULL;
    }

    err_code = read_fat_data (volume);
    if (err_code != SUCCESS) {
        handle_errno(err_code, volume);
        return NULL;
    }

    return volume;
}

int read_super_sector (struct disk_t *pdisk, struct volume_t * volume) {
    int err_code = disk_read(pdisk, 0, &volume->super_sector, 1);
    if (err_code == -1) return DISK_READ_FAULT;
    return SUCCESS;
}

int read_fats (struct disk_t *pdisk, struct volume_t * volume) {
    size_t bytes_per_fat = volume->super_sector.sectors_per_fat * volume->super_sector.bytes_per_sector;

    volume->fat_1 = (uint8_t *)malloc (bytes_per_fat);
    volume->fat_2 = (uint8_t *)malloc (bytes_per_fat);
    if (volume->fat_1 == NULL || volume->fat_2 == NULL) return NOMEM;

    int err_code = disk_read (pdisk, volume->geometry.fat_1_position, volume->fat_1, volume->super_sector.sectors_per_fat);
    if (err_code == -1) return DISK_READ_FAULT;

    err_code = disk_read (pdisk, volume->geometry.fat_2_position, volume->fat_2, volume->super_sector.sectors_per_fat);
    if (err_code == -1) return DISK_READ_FAULT;

    if (memcmp(volume->fat_1, volume->fat_2, bytes_per_fat) != 0) return CORRUPTED;

    return SUCCESS;
}

int read_root_dir (struct disk_t *pdisk, struct volume_t * volume) {
    size_t rootdir_bytes = volume->geometry.rootdir_size * volume->super_sector.bytes_per_sector;
    volume->root_directory = (struct fat_sfn_t *)malloc (rootdir_bytes);
    if (volume->root_directory == NULL) return NOMEM;

    int err_code = disk_read (pdisk, volume->geometry.rootdir_position, volume->root_directory, volume->geometry.rootdir_size);
    if (err_code == -1) return DISK_READ_FAULT;

    return SUCCESS;
}

int read_data_area (struct disk_t *pdisk, struct volume_t * volume) {
    size_t dataarea_bytes = volume->geometry.user_space * volume->super_sector.bytes_per_sector;
    volume->data_area = (uint8_t *)malloc (dataarea_bytes);
    if (volume->data_area == NULL) return NOMEM;

    int err_code = disk_read (pdisk, volume->geometry.cluster2_position, volume->data_area, volume->geometry.user_space);
    if (err_code == -1) return DISK_READ_FAULT;

    return SUCCESS;
}

void handle_errno (int err_code, struct volume_t *vol) {
    if (err_code == DISK_READ_FAULT) fat_close (vol);

    if (err_code == CORRUPTED) {
        fat_close (vol);
        errno = EINVAL;
    }

    if (err_code == NOMEM) {
        fat_close (vol);
        errno = ENOMEM;
    }
}

void calculate_volume_geometry (struct volume_t *volume) {
    if (volume == NULL) return;
    volume->geometry.volume_start = 0;
    volume->geometry.fat_1_position = volume->geometry.volume_start + volume->super_sector.reserved_sectors;
    volume->geometry.fat_2_position = volume->geometry.fat_1_position + volume->super_sector.sectors_per_fat;
    volume->geometry.rootdir_position = volume->geometry.volume_start + volume->super_sector.reserved_sectors +
            volume->super_sector.fat_count * volume->super_sector.sectors_per_fat;

    volume->geometry.rootdir_size = (volume->super_sector.root_dir_capacity * sizeof(struct fat_sfn_t)) /
            (int)volume->super_sector.bytes_per_sector;

    if (volume->super_sector.root_dir_capacity * sizeof(struct fat_sfn_t) %
            (int)volume->super_sector.bytes_per_sector != 0) volume->geometry.rootdir_size += 1;

    volume->geometry.cluster2_position = volume->geometry.rootdir_position + volume->geometry.rootdir_size;
    volume->geometry.volume_size = volume->super_sector.logical_sectors16 == 0 ?
            volume->super_sector.logical_sectors32 : volume->super_sector.logical_sectors16;

    volume->geometry.user_space = volume->geometry.volume_size - volume->super_sector.reserved_sectors -
            volume->super_sector.fat_count * volume->super_sector.sectors_per_fat - volume->geometry.rootdir_size;

    volume->geometry.total_clusters = volume->geometry.user_space / volume->super_sector.sectors_per_cluster + 1;
}

int validate_super_sector (const struct fat_super_t super) {
    if (super.sectors_per_cluster < 1 || super.sectors_per_cluster > 128) return 1;
    if (super.reserved_sectors <= 0) return 1;
    if (super.fat_count < 1 || super.fat_count > 2) return 1;
    if (!(super.logical_sectors32 == 0 ^ super.logical_sectors16 == 0)) return 1;
    return 0;
}

int read_fat_data (struct volume_t *volume) {
    volume->fat_data = (uint16_t *)malloc(sizeof(uint16_t) * volume->geometry.total_clusters);
    if (volume->fat_data == NULL) return NOMEM;

    unsigned int i = 0;
    for (unsigned int j = 0; i < volume->geometry.total_clusters; j+=3) {
        uint8_t b0 = volume->fat_1[j + 0];
        uint8_t b1 = volume->fat_1[j + 1];
        uint8_t b2 = volume->fat_1[j + 2];

        uint16_t c0 = ((uint16_t)(b1 & 0x0F) << 8) | b0;
        uint16_t c1 = ((uint16_t)b2 << 4) | ((b1 & 0xF0) >> 4);

        volume->fat_data[i + 0] = c0;
        volume->fat_data[i + 1] = c1;
        i += 2;
    }

    return SUCCESS;
}

int fat_close (struct volume_t* pvolume) {
    if (pvolume == NULL) {
        errno = EFAULT;
        return -1;
    }

    free (pvolume->fat_1);
    free (pvolume->fat_2);
    free (pvolume->root_directory);
    free (pvolume->data_area);
    free (pvolume->fat_data);
    free (pvolume);

    return 0;
}

struct file_t* file_open (struct volume_t* pvolume, const char* file_name) {
    if (pvolume == NULL || file_name == NULL) {
        errno = EFAULT;
        return NULL;
    }

    struct fat_sfn_t *file_entry = search_for_file (pvolume, file_name);
    if (file_entry == NULL) {
        errno = ENOENT;
        return NULL;
    }

    if ((file_entry->file_attribute & FAT_ATTRIB_LABEL) != 0 ||
            (file_entry->file_attribute & FAT_ATTRIB_DIR) != 0) {
        errno = EISDIR;
        return NULL;
    }

    struct file_t *result = malloc (sizeof(struct file_t));
    if (result == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    result->size = file_entry->file_size;
    result->data = malloc(result->size + 1);
    if (result->data == NULL) {
        errno = ENOMEM;
        free(result);
        return NULL;
    }

    result->curr_position = 0;
    uint32_t bytes_to_read = result->size;
    int pos = 0;
    cluster_t cluster = file_entry->file_first_low;
    while (cluster != pvolume->fat_data[1]) {
        if (bytes_to_read >= pvolume->super_sector.bytes_per_sector*pvolume->super_sector.sectors_per_cluster) {
            for (int i = 0; i < pvolume->super_sector.bytes_per_sector*pvolume->super_sector.sectors_per_cluster; ++i) {
                result->data[pos++] = pvolume->data_area[(cluster - 2)*pvolume->super_sector.bytes_per_sector*pvolume->super_sector.sectors_per_cluster + i];
            }
            bytes_to_read -= pvolume->super_sector.bytes_per_sector*pvolume->super_sector.sectors_per_cluster;
        } else {
            for (unsigned int i = 0; i < bytes_to_read; ++i) {
                result->data[pos++] = pvolume->data_area[(cluster - 2)*pvolume->super_sector.bytes_per_sector*pvolume->super_sector.sectors_per_cluster + i];
            }
            result->data[pos] = '\0';
            return result;
        }
        cluster = get_next_cluster(pvolume, cluster);
    }

    result->data[pos] = '\0';
    return result;
}

struct fat_sfn_t * search_for_file (struct volume_t* pvolume, const char* file_name) {
    for (int i = 0; i < pvolume->super_sector.root_dir_capacity; ++i) {
        if (pvolume->root_directory[i].file_name[0] == '\0') break;
        char *full_filename = make_name (pvolume->root_directory[i].file_name);
        if (full_filename == NULL) return NULL;
        if (memcmp(file_name, full_filename, strlen(file_name)) == 0) {
            free(full_filename);
            return &pvolume->root_directory[i];
        }
        free (full_filename);
    }
    return NULL;
}

char *make_name (const uint8_t *file_name) {
    char *result = malloc (8+3+2);
    if (result == NULL) return NULL;

    int i = 0;
    for (; i < 8; ++i) {
        if (file_name[i] == ' ') break;
        result[i] = file_name[i];
    }

    if (file_name[9] == ' ') {
        result[i] = '\0';
        return result;
    }

    result[i++] = '.';
    for (int j = 0; j < 3; ++j) {
        if (file_name[j + 8] == ' ') break;
        result[i++] = file_name[j+8];
    }
    result[i] = '\0';
    return result;
}

cluster_t get_next_cluster (struct volume_t *volume, cluster_t current) {
    return volume->fat_data[current];
}

int file_close (struct file_t* stream) {
    if (stream == NULL) {
        errno = EFAULT;
        return -1;
    }

    free (stream->data);
    free (stream);

    return 0;
}

size_t file_read (void *ptr, size_t size, size_t nmemb, struct file_t *stream) {
    if (ptr == NULL || stream == NULL) {
        errno = EFAULT;
        return -1;
    }

    int result = 0;
    for (unsigned int i = 0; i < nmemb; ++i) {
        if (stream->curr_position == stream->size) break;
        for (unsigned int j = 0; j < size; ++j) {
            if (stream->curr_position == stream->size) return result;
            *((char *)ptr + i + j) = stream->data[stream->curr_position];
            stream->curr_position++;
        }
        result++;
    }

    return result;
}

int32_t file_seek (struct file_t* stream, int32_t offset, int whence) {
    if (stream == NULL) {
        errno = EFAULT;
        return -1;
    }

    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
        errno = EINVAL;
        return -1;
    }

    if (whence == SEEK_SET) {
        if (offset > stream->size) {
            errno = ENXIO;
            return -1;
        }
        stream->curr_position = offset;
    }

    if (whence == SEEK_CUR) {
        if (offset + stream->curr_position > stream->size) {
            errno = ENXIO;
            return -1;
        }
        stream->curr_position += offset;
    }

    if (whence == SEEK_END) {
        if (stream->size + offset < 0) {
            errno = ENXIO;
            return -1;
        }
        stream->curr_position = stream->size + offset;
    }

    return stream->curr_position;
}

struct dir_t* dir_open (struct volume_t* pvolume, const char* dir_path) {
    if (pvolume == NULL) {
        errno = EFAULT;
        return NULL;
    }

    if (strcmp(dir_path, "\\") != 0) {
        errno = ENOENT;
        return NULL;
    }

    struct dir_t * result = malloc(sizeof(struct dir_t));
    if (result == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    result->content = malloc(sizeof(struct dir_entry_t) * pvolume->super_sector.root_dir_capacity);
    if (result->content == NULL) {
        errno = ENOMEM;
        free(result);
        return NULL;
    }

    result->current = 0;
    result->num_of_elements = 0;

    for (int i = 0; i < pvolume->super_sector.root_dir_capacity; ++i) {
        if (pvolume->root_directory[i].file_name[0] == '\0') break;
        if ((pvolume->root_directory[i].file_attribute & FAT_ATTRIB_LABEL) != 0) continue;
        if (pvolume->root_directory[i].file_name[0] == 0xe5) continue;
        fill_dir_entry(&result->content[result->num_of_elements], &pvolume->root_directory[i]);
        result->num_of_elements++;
    }

    return result;
}

void fill_dir_entry(struct dir_entry_t *entry, const struct fat_sfn_t *sfn) {
    fill_name (entry, sfn);
    entry->size = sfn->file_size;
    fill_attributes (entry, sfn);
    fill_date (entry, sfn);
    fill_time(entry, sfn);
}

void fill_name(struct dir_entry_t *entry, const struct fat_sfn_t *sfn) {
    int i = 0;
    for (; i < 8; ++i) {
        if (sfn->file_name[i] == ' ') break;
        entry->name[i] = sfn->file_name[i];
    }

    if (sfn->file_name[9] == ' ') {
        entry->name[i] = '\0';
        return;
    }

    entry->name[i++] = '.';
    for (int j = 0; j < 3; ++j) {
        if (sfn->file_name[j + 8] == ' ') break;
        entry->name[i++] = sfn->file_name[j+8];
    }
    entry->name[i] = '\0';
}


void fill_attributes(struct dir_entry_t *entry, const struct fat_sfn_t *sfn) {
    clear_attributes (entry);
    if (extract_bits(sfn->file_attribute, 1, 1)) entry->is_readonly = 1;
    if (extract_bits(sfn->file_attribute, 1, 2)) entry->is_hidden = 1;
    if (extract_bits(sfn->file_attribute, 1, 3)) entry->is_system = 1;
    if (extract_bits(sfn->file_attribute, 1, 5)) entry->is_directory = 1;
    if (extract_bits(sfn->file_attribute, 1, 6)) entry->is_archived = 1;
}

void clear_attributes (struct dir_entry_t *entry) {
    entry->is_archived = 0;
    entry->is_readonly = 0;
    entry->is_system = 0;
    entry->is_hidden = 0;
    entry->is_directory = 0;
}

void fill_date(struct dir_entry_t *entry, const struct fat_sfn_t *sfn) {
    entry->creation_date.day = extract_bits(sfn->file_creation_date, 5, 1);
    entry->creation_date.month = extract_bits(sfn->file_creation_date, 4, 6);
    entry->creation_date.year = extract_bits(sfn->file_creation_date, 7, 10) + 1980;
}

void fill_time (struct dir_entry_t *entry, const struct fat_sfn_t *sfn) {
    entry->creation_time.hour =  extract_bits(sfn->file_creation_time, 5, 12);
    entry->creation_time.minute = extract_bits(sfn->file_creation_time, 6, 6);
    entry->creation_time.second = extract_bits(sfn->file_creation_time, 5,1);
}

int extract_bits(int number, int k, int p) {
    return (((1 << k) - 1) & (number >> (p - 1)));
}

int dir_close (struct dir_t* pdir) {
    if (pdir == NULL) {
        errno = EFAULT;
        return -1;
    }

    free(pdir->content);
    free(pdir);
    return 0;

}

int dir_read (struct dir_t* pdir, struct dir_entry_t* pentry) {
    if (pdir == NULL || pentry == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (pdir->current == pdir->num_of_elements) return 1;

    memcpy(pentry, &pdir->content[pdir->current], sizeof(struct dir_entry_t));
    pdir->current++;

    return 0;
}
