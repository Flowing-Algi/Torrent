#include <iostream>
#include <fstream>
#include <ios>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
/* include */
#include "lzma_wrapper.h"
#include "log.h"
/* extern */
#include "C/LzmaLib.h"
#include "C/7zTypes.h"
#include "C/Alloc.h"
#include "C/LzmaEnc.h"
#include "C/LzmaDec.h"

#define LZMA_PROPS_SIZE_FILESIZE LZMA_PROPS_SIZE + 8

/* function implementation for struct ISzAlloc 
 * see examples in LzmaUtil.c and C/alloc.c
 */

static void *SzAlloc(void *p, size_t size)
{
    (void)p; // silence unused var warning
    return MyAlloc(size); // just a malloc call...
}

/* function implementation for struct ISzAlloc 
 * see examples in LzmaUtil.c and C/alloc.c
 */
static void SzFree(void *p, void *address)
{
    (void)p; // silence unused var warning
    MyFree(address); // just a free call ... 
}

ISzAlloc g_Alloc = {SzAlloc, SzFree};

/* Read raw data from a filedescriptor
 * INPUT:
 * FILE *fd - file descriptor
 * unsigned char *data - buffer that will hold the read
 * data
 * size_t *data_len - the size of the read data, this will
 * also indicate when we have reached the end of the file
 * OUTPUT:
 * size of data read
 */
static size_t my_read_data(FILE *fd, void *data, size_t data_len)
{
    if (data_len == 0)
        return 0;
    return fread(data, sizeof(unsigned char), data_len, fd);
}

/* implementation of ISeqoutstream->read */
static int read_data(void *p, void *data, size_t *data_len)
{
    if (*data_len == 0)
        return SZ_OK;
    *data_len = my_read_data(((seq_in_stream *)p)->fd, data, *data_len);
        return SZ_OK;
}

/* write raw data to a filedescriptor
 * INPUT:
 * FILE *fd - file descriptor
 * unsigned char *data - buffer holds the data
 * size_t *data_len - the size of the data buffer
 * OUTPUT:
 * number of bytes written
 */
static size_t my_write_data(FILE *fd, const void *data, size_t data_len)
{
    if (data_len == 0)
        return 0;
    return fwrite(data, sizeof(unsigned char), data_len, fd);
}

/* implementation of ISeqinstream->write */
static size_t write_data(void *p, const void *data, size_t data_len)
{
    return my_write_data(((seq_out_stream *)p)->fd, data, data_len );
}

/* Open two files, one for input one for output
 * INPUT:
 * const char *in_path - path to input file
 * const char *out_path - path to output file
 */
static int open_io_files(const char *in_path, const char*out_path,
                         FILE *fd[])
{
    fd[0] = fopen(in_path, "r");
    if (fd[0] == NULL)  {
        log_msg("Error opening input file for compression: %s\n",
                strerror(errno));
        goto cleanup;
    }
    fd[1] = fopen(out_path, "w+");
    if (fd[0] == NULL)  {
        log_msg("Error opening output file for compression: %s\n",
                strerror(errno));
        goto cleanup;
    }
    return 1;

 cleanup:
    if (fd[0] != NULL)
        fclose(fd[0]);
    if (fd[1] != NULL)
        fclose(fd[1]);
    return 0;
}

/* read the prop, the following 8 bits hold the file
 * uncompressed size, get those in little endian form
 */
static unsigned long get_header(FILE *fd, unsigned char *props_header, size_t len)
{
    unsigned long rt = 0; // return val (file size)
    if (len != my_read_data(fd, props_header, len))
        return 0;
    for (int i = 0; i < 8; i++) {
        /* get file size, stored little endian after prop */
        rt |= (unsigned long)(props_header[LZMA_PROPS_SIZE + i]
                             << (i * 8));
    }
    return rt;
}

/* work in progress, wrapper fn for compression call */
int compress_file(const char *in_path,
                  const char *out_path,
                  void *args)
{
    FILE *fd[2]; /* i/o file descriptors */
    /* open i/o files, return fail if this failes */
    if (!open_io_files(in_path, out_path, fd))
        return 0;
    compress_data_incr(fd[0], fd[1], args);

    if (fd[0] != NULL)
        fclose(fd[0]);
    if (fd[1] != NULL)
        fclose(fd[1]);
    return 1;
}

int decompress_file(const char *in_path,
                    const char *out_path,
                    void *args)
{
    FILE *fd[2]; /* i/o file descriptors */
    /* open i/o files, return fail if this failes */
    if (!open_io_files(in_path, out_path, fd))
        return 0;
    decompress_data_incr(fd[0], fd[1]);

    if (fd[0] != NULL)
        fclose(fd[0]);
    if (fd[1] != NULL)
        fclose(fd[1]);
    return 1;
}

int compress_data_incr(FILE *input, FILE *output, void *args)
{
    int rt = 1;
    /* iseqinstream and iseqoutstream objects */
    seq_in_stream i_stream = {{read_data}, input};
    seq_out_stream o_stream = {{write_data}, output};
    /* CLzmaEncHandle is just a pointer (void *) */
    CLzmaEncHandle enc_hand = LzmaEnc_Create(&g_Alloc);
    if (enc_hand == NULL) {
        log_msg ("Error allocating mem when reading stream");
        return SZ_ERROR_MEM;
    }
    /* 5 bytes for lzma prop + 8 bytes for filesize */
    unsigned char props_header[LZMA_PROPS_SIZE_FILESIZE];
    unsigned int props_size = LZMA_PROPS_SIZE; // size of prop
    unsigned long file_size = get_file_size_c(input); // filesize
    CLzmaEncProps prop_info; // info for prop, control vals for comp
    /* note the prop is the header of the compressed file */
    LzmaEncProps_Init(&prop_info);
    prop_info.algo = 0;
    prop_info.dictSize = 1 << 16;
    prop_info.fb = 128;
    rt = LzmaEnc_SetProps(enc_hand, &prop_info);
    printf("algo:%d\n"
           "dict:%ld\n"
           "thread:%d\n", prop_info.algo, prop_info.dictSize, prop_info.numThreads);
    if (rt != SZ_OK)
        goto end;
    
    rt = LzmaEnc_WriteProperties(enc_hand, props_header, &props_size);
    for (int i = 0; i < 8; i++) {
        /* store filesize little endian after prop, easier
        * to read back in
        */
        props_header[props_size++] = (unsigned char)(file_size
                                                     >> (8 * i));
    }
    write_data((void*)&o_stream, props_header, props_size);
    if (rt == SZ_OK)
        rt = LzmaEnc_Encode(enc_hand,
                            &(o_stream.out_stream),
                            &(i_stream.in_stream),
                            NULL, &g_Alloc, &g_Alloc);
    LzmaEnc_Destroy(enc_hand, &g_Alloc, &g_Alloc);
    return 1;

 end:
    log_msg("Error occurred compressing data: LZMA errno %d\n", rt);
    LzmaEnc_Destroy(enc_hand, &g_Alloc, &g_Alloc);
    return rt;
}

int decompress_data_incr(FILE *input, FILE *output)
{
    unsigned long file_size = 0; // size of file
    int rt; // return val
    unsigned char props_header[LZMA_PROPS_SIZE_FILESIZE];
    CLzmaDec state; // view in LzmaDec.h

    file_size = get_header(input, props_header, LZMA_PROPS_SIZE_FILESIZE);
    if (file_size == 0)
        return 0; // failed
    LzmaDec_Construct(&state);
    rt = LzmaDec_Allocate(&state, props_header, LZMA_PROPS_SIZE, &g_Alloc);
    if (rt != SZ_OK)
        /*goto error */
        ;
    unsigned char in_buff[buffer_cread_size];
    unsigned char out_buff[buffer_cread_size];
    unsigned long out_pos = 0, in_read_size = 0, in_pos = 0;
    unsigned int in_processed = 0, out_processed = 0;
    ELzmaFinishMode fin_mode = LZMA_FINISH_ANY;
    ELzmaStatus status;
    LzmaDec_Init(&state);
    while(1) {
        if (in_pos == in_read_size) {
            in_read_size = my_read_data(input, in_buff, buffer_cread_size);
            in_pos = 0;
        } else {
            in_processed = (unsigned int)(in_read_size - in_pos);
            out_processed = (unsigned int)(buffer_cread_size - out_pos);
            if (out_processed > file_size) {
                out_processed = (unsigned int)file_size;
                fin_mode = LZMA_FINISH_END;
            }
            rt = LzmaDec_DecodeToBuf(&state,
                                     out_buff + out_pos,
                                     &out_processed,
                                     in_buff + in_pos,
                                     &in_processed,
                                     fin_mode,
                                     &status);
            in_pos += in_processed;
            out_pos += out_processed;
            file_size -= out_processed;

            my_write_data(output, out_buff, out_pos);
            out_pos = 0;

            if ((rt != SZ_OK) || (file_size == 0))
                break;
            if ((in_processed == 0) && (out_processed == 0)) {
                log_msg("ERROR OCCURRED ECOMPRESS\n");
                break;
            }
        }
    }
    LzmaDec_Free(&state, &g_Alloc);
    return rt;
    
}
