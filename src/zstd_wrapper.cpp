/** @brief Open two files, one for input one for output
 *
 * @return
 * 1 - success \n
 * 0 - failure
 */
#include <stdlib.h>    // malloc, exit
#include <stdio.h>     // fprintf, perror, feof
#include <string.h>    // strerror
#define ZSTD_STATIC_LINKING_ONLY  // streaming API defined as "experimental" for the time being
#include "zstd.h"      // presumes zstd library is installed

#ifdef _WIN32
#include <limits.h>
#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif//PATH_MAX
#else
#include <linux/limits.h>
#endif//WINDOWS

#include "log.h"
#include "zstd_wrapper.h"

static void *my_malloc(size_t s)
{
    void *const tmp = malloc(s);
    if (tmp)
        return tmp;
    log_msg_default;
    return NULL;
}

static c_resources create_cresources()
{
    c_resources z;
    z.i_buff_size = ZSTD_CStreamInSize(); // 1 << 17
    z.o_buff_size = ZSTD_CStreamOutSize();
    // allocate i/o buffs
    z.i_buff = my_malloc(z.i_buff_size);
    z.o_buff = my_malloc(z.o_buff_size);
    z.c_stream = ZSTD_createCStream();
    if (!z.c_stream) {
        log_msg_custom("ZSTD_createCstream() error");
        exit(2);
    }
    return z;
}

static d_resources create_dresources()
{
    d_resources z;
    z.i_buff_size = ZSTD_DStreamInSize(); // 1 << 17
    z.o_buff_size = ZSTD_DStreamOutSize();
    // allocate i/o buffs
    z.i_buff = my_malloc(z.i_buff_size);
    z.o_buff = my_malloc(z.o_buff_size);
    z.d_stream = ZSTD_createDStream();
    if (!z.d_stream) {
        log_msg_custom("ZSTD_createDstream() error");
        exit(2);
    }
    return z;
}

static void free_cresources(c_resources z) {
    ZSTD_freeCStream(z.c_stream);
    if (z.i_buff)
        free(z.i_buff);
    if (z.o_buff)
        free(z.o_buff);
}

static void free_dresources(d_resources z) {
    ZSTD_freeDStream(z.d_stream);
    if (z.i_buff)
        free(z.i_buff);
    if (z.o_buff)
        free(z.o_buff);
}

/**
 * @brief Read raw data from a filedescriptor
 * INPUT:
 * @return
 * Size of data read\n
 * @p data Will contain the information read from fd
 */
static size_t my_read_data(FILE *fd, //!< Fd to input
                           void *data, //!< Buf to hold data
                           size_t data_len //!< Size to read, also indicates if the file is done being read
                           )
{
    if (data_len == 0)
        return 0;
    return fread(data, sizeof(unsigned char), data_len, fd);
}

/** @brief Write raw data to a filedescriptor
 * 
 * @return
 * Number of bytes written
 */
static size_t my_write_data(FILE *fd, //!< Fp to dest
                            const void *data, //!< Data to write
                            size_t data_len //!< Size of @p data 
                            ) 
{
    if (data_len == 0)
        return 0;
    return fwrite(data, sizeof(unsigned char), data_len, fd);
}

/** @brief Open two files, one for input one for output
 *
 * @return
 * 1 - success \n
 * 0 - failure
 */
static int open_io_files(const char *in_path, //!< Input path
                         const char*out_path, //!< Output path
                         FILE *fd[] //!< Array of FP, should change to individual Fps
                         )
{
    fd[0] = fopen(in_path, "rb");
    if (!fd[0]) {
        log_msg_default;
        goto cleanup;
    }
    fd[1] = fopen(out_path, "wb+");
    if (!fd[1])  {
        char msg[60];
        snprintf(msg, 59, "\nOpening [%s] failed\n", out_path);
        log_msg_custom(msg);
        goto cleanup;
    }
    return 1;

 cleanup:
    if (fd[0])
        fclose(fd[0]);
    if (fd[1])
        fclose(fd[1]);
    return 0;
}

/** @brief Set the outputs file name for compression
 *
 * If out_path is null it will set the output name by concatting .7z to
 * input name, this is for compression only
 *
 * @return
 * @p out_path_local will hold the correct out_path
 */
static void set_comp_out_file_name(const char *in_path, //!< Input path
                                   const char *out_path, //!< Output path
                                   char *out_path_local //!< Output path after fn call
                                   )
{
    if (out_path == NULL)
        snprintf(out_path_local, PATH_MAX,
                 "%s.zstd", in_path);
    else
        snprintf(out_path_local, PATH_MAX,
                 "%s", out_path);
}

int zcompress_file(const char * in_path,
                   const char *out_path,
                   int c_level)
{
    if (in_path == NULL) {
        log_msg("Invalid args");
        return 0;
    }
    char out_path_local[PATH_MAX];
    set_comp_out_file_name(in_path, out_path,
                           out_path_local);

    FILE *fd[2]; /* i/o file descriptors */
    fd[0] = NULL; fd[1] = NULL;
    /* open i/o files, return fail if this failes */
    if (!open_io_files(in_path, out_path_local, fd))
        return 0;

    printf("compressing %s -> %s\n", in_path, out_path_local);

    zcompress_data(fd[0], fd[1], c_level);

    if (fd[0])
        fclose(fd[0]);
    if (fd[1])
        fclose(fd[1]);
    return 1;
}

int zcompress_data(FILE *fd_i, FILE *fd_o, int c_level)
{
    c_resources const z = create_cresources(); // init values for compression
    size_t const init_z = ZSTD_initCStream( z.c_stream, c_level); // set compression level
    if (ZSTD_isError(init_z)) {
        log_msg_custom(ZSTD_getErrorName(init_z));
        exit(2);
    }

    size_t read = 0, to_read = z.i_buff_size; // read size
    while ((read = my_read_data(fd_i, z.i_buff, to_read))) {
        ZSTD_inBuffer input = {z.i_buff, read, 0};
        while (input.pos < input.size) {
            ZSTD_outBuffer output = {z.o_buff, z.o_buff_size, 0};
            // to_read will hold the size of the next block to read
            to_read = ZSTD_compressStream(z.c_stream, &output, &input);
            if (ZSTD_isError(to_read)) {
                log_msg_custom(ZSTD_getErrorName(to_read));
                exit(2);
            }
            if (to_read > z.i_buff_size)
                to_read = z.i_buff_size;
            my_write_data(fd_o, z.o_buff, output.pos);
        }
    }

    ZSTD_outBuffer output = {z.o_buff, z.o_buff_size, 0};
    size_t const remaining_to_flush = ZSTD_endStream(
        z.c_stream,
        &output);
    if (remaining_to_flush) {
        log_msg_custom("Not Fully Flushed");
        exit(2);
    }
    my_write_data(fd_o, z.o_buff, output.pos);

    free_cresources(z);
    return 1;
}

/** @brief Set the output filename for decompression
 *
 * If out_path is null set the output file name by putting a string
 * terminator where .7z is
 *
 * @return
 * @p out_path_local will hold the correct output path
 */
static void set_decomp_out_file_name(const char *in_path, //!< Input path
                                     const char *out_path, //!< Output path
                                     char *out_path_local //!< Output path after fn call
                                     )
{
    if (out_path == NULL) {
        snprintf(out_path_local, PATH_MAX,
                 "%s", in_path);
        char * tmp = strstr(out_path_local,".zstd");
        tmp[0] = '\0';
    }
    else {
        snprintf(out_path_local, PATH_MAX,
                 "%s", out_path);
    }
}
int zdecompress_file(const char *in_path,
                    const char *out_path)
{
    if (in_path == NULL) {
        log_msg("Invalid args to decompress_file");
        return 0;
    }
    char out_path_local[PATH_MAX];
    set_decomp_out_file_name(in_path, out_path,
                             out_path_local);

    FILE *fd[2]; /* i/o file descriptors */
    fd[0] = NULL; fd[1] = NULL;
    
    /* open i/o files, return fail if this failes */
    if (!open_io_files(in_path, out_path_local, fd))
        return 0;
    printf("decompressing %s -> %s\n", in_path, out_path_local);

    if (!zdecompress_data(fd[0], fd[1]))
        log_msg("Failed to decompress\n");

    if (fd[0])
        fclose(fd[0]);
    if (fd[1])
        fclose(fd[1]);
    return 1;
}

int zdecompress_data(FILE *fd_i, FILE *fd_o)
{
    d_resources const z = create_dresources(); // init values for compression
    size_t init_z = ZSTD_initDStream(z.d_stream);
    if (ZSTD_isError(init_z)) {
        log_msg_custom(ZSTD_getErrorName(init_z));
        exit(2);
    }

    size_t read = 0, to_read = init_z; // read size
    while ((read = my_read_data(fd_i, z.i_buff, to_read))) {
        ZSTD_inBuffer input = {z.i_buff, read, 0};
        while (input.pos < input.size) {
            ZSTD_outBuffer output = {z.o_buff, z.o_buff_size, 0};
            // to_read will now hold the size of the next block to read
            to_read = ZSTD_decompressStream(z.d_stream, &output, &input);
            if (ZSTD_isError(to_read)) {
                log_msg_custom(ZSTD_getErrorName(to_read));
                exit(2);
            }
            my_write_data(fd_o, z.o_buff, output.pos);
        }
    }

    free_dresources(z);
    return 1;
}
