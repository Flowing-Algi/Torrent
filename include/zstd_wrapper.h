/**
 * @file zstd_wrapper.h
 * @brief zstd library implementation
 *
 * Testing
 */

#include "zstd.h"      // presumes zstd library is installed

typedef struct c_resources c_resources;
struct c_resources {
    void *i_buff;
    void *o_buff;
    size_t i_buff_size;
    size_t o_buff_size;
    ZSTD_CStream *c_stream;
};

typedef struct d_resources d_resources;
struct d_resources {
    void *i_buff;
    void *o_buff;
    size_t i_buff_size;
    size_t o_buff_size;
    ZSTD_DStream *d_stream;
};
int zcompress_data(FILE *fd_i, FILE *fd_o, int c_level);

int zcompress_file(const char * in_path,
                   const char *out_path = NULL,
                   int c_level = 1);

int zdecompress_file(const char *in_path,
                     const char *out_path);

int zdecompress_data(FILE *fd_i, FILE *fd_o);
