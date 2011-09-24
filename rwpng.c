/*---------------------------------------------------------------------------

   pngquant:  RGBA -> RGBA-palette quantization program             rwpng.c

  ---------------------------------------------------------------------------

      Copyright (c) 1998-2000 Greg Roelofs.  All rights reserved.

      This software is provided "as is," without warranty of any kind,
      express or implied.  In no event shall the author or contributors
      be held liable for any damages arising in any way from the use of
      this software.

      Permission is granted to anyone to use this software for any purpose,
      including commercial applications, and to alter it and redistribute
      it freely, subject to the following restrictions:

      1. Redistributions of source code must retain the above copyright
         notice, disclaimer, and this list of conditions.
      2. Redistributions in binary form must reproduce the above copyright
         notice, disclaimer, and this list of conditions in the documenta-
         tion and/or other materials provided with the distribution.
      3. All advertising materials mentioning features or use of this
         software must display the following acknowledgment:

            This product includes software developed by Greg Roelofs
            and contributors for the book, "PNG: The Definitive Guide,"
            published by O'Reilly and Associates.

  ---------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "png.h"        /* libpng header; includes zlib.h */
#include "rwpng.h"      /* typedefs, common macros, public prototypes */

static void rwpng_error_handler(png_structp png_ptr, png_const_charp msg);


void rwpng_version_info(void)
{
    fprintf(stderr, "   Compiled with libpng %s; using libpng %s.\n",
      PNG_LIBPNG_VER_STRING, png_get_header_ver(NULL));
    fprintf(stderr, "   Compiled with zlib %s; using zlib %s.\n",
      ZLIB_VERSION, zlib_version);
}



/*
   retval:
     0 = success
    21 = bad sig
    22 = bad IHDR
    24 = insufficient memory
    25 = libpng error (via longjmp())
    26 = wrong PNG color type (no alpha channel)
 */

pngquant_error rwpng_read_image(FILE *infile, read_info *mainprog_ptr)
{
    png_structp  png_ptr = NULL;
    png_infop    info_ptr = NULL;
    png_uint_32  i, rowbytes;
    int          color_type, bit_depth;
    unsigned char sig[8];


    /* first do a quick check that the file really is a PNG image; could
     * have used slightly more general png_sig_cmp() function instead */

    if (!fread(sig, 8, 1, infile)) {
        return READ_ERROR;
    }

    if (png_sig_cmp(sig, 0, 8)) {
        return BAD_SIGNATURE_ERROR;   /* bad signature */
    }


    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, mainprog_ptr,
      rwpng_error_handler, NULL);
    if (!png_ptr) {
        return PNG_OUT_OF_MEMORY_ERROR;   /* out of memory */
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        return PNG_OUT_OF_MEMORY_ERROR;   /* out of memory */
    }


    /* GRR TO DO:  use end_info struct */
    /* we could create a second info struct here (end_info), but it's only
     * useful if we want to keep pre- and post-IDAT chunk info separated
     * (mainly for PNG-aware image editors and converters) */


    /* setjmp() must be called in every function that calls a non-trivial
     * libpng function */

    if (setjmp(mainprog_ptr->jmpbuf)) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return LIBPNG_FATAL_ERROR;   /* fatal libpng error (via longjmp()) */
    }


    png_init_io(png_ptr, infile);
    png_set_sig_bytes(png_ptr, 8);  /* we already read the 8 signature bytes */

    png_read_info(png_ptr, info_ptr);  /* read all PNG info up to image data */


    /* alternatively, could make separate calls to png_get_image_width(),
     * etc., but want bit_depth and color_type for later [don't care about
     * compression_type and filter_type => NULLs] */

    png_get_IHDR(png_ptr, info_ptr, &mainprog_ptr->width, &mainprog_ptr->height,
      &bit_depth, &color_type, &mainprog_ptr->interlaced, NULL, NULL);


    /* expand palette images to RGB, low-bit-depth grayscale images to 8 bits,
     * transparency chunks to full alpha channel; strip 16-bit-per-sample
     * images to 8 bits per sample; and convert grayscale to RGB[A] */

    /* GRR TO DO:  handle each of GA, RGB, RGBA without conversion to RGBA */
    /* GRR TO DO:  allow sub-8-bit quantization? */
    /* GRR TO DO:  preserve all safe-to-copy ancillary PNG chunks */
    /* GRR TO DO:  get and map background color? */

    if (!(color_type & PNG_COLOR_MASK_ALPHA)) {
        /* GRP:  expand palette to RGB, and grayscale or RGB to GA or RGBA */
        if (color_type == PNG_COLOR_TYPE_PALETTE)
            png_set_expand(png_ptr);
        png_set_filler(png_ptr, 65535L, PNG_FILLER_AFTER);
    }
/*
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand(png_ptr);
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
        png_set_expand(png_ptr);
 */
    /* GRR TO DO:  handle 16-bps data natively? */
    if (bit_depth == 16)
        png_set_strip_16(png_ptr);
    /* GRR TO DO:  probably want to handle this separately, without expansion */
    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png_ptr);


    /* get and save the gamma info (if any) for writing */

    if (!png_get_gAMA(png_ptr, info_ptr, &mainprog_ptr->gamma)) {
        mainprog_ptr->gamma = 0.45455;
    }

    /* all transformations have been registered; now update info_ptr data,
     * get rowbytes and channels, and allocate image memory */

    png_read_update_info(png_ptr, info_ptr);

    mainprog_ptr->rowbytes = rowbytes = png_get_rowbytes(png_ptr, info_ptr);
    //mainprog_ptr->channels = (int)png_get_channels(png_ptr, info_ptr);

    if ((mainprog_ptr->rgba_data = malloc(rowbytes*mainprog_ptr->height)) == NULL) {
        fprintf(stderr, "pngquant readpng:  unable to allocate image data\n");
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return PNG_OUT_OF_MEMORY_ERROR;
    }
    if ((mainprog_ptr->row_pointers = (png_bytepp)malloc(mainprog_ptr->height*sizeof(png_bytep))) == NULL) {
        fprintf(stderr, "pngquant readpng:  unable to allocate row pointers\n");
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        free(mainprog_ptr->rgba_data);
        mainprog_ptr->rgba_data = NULL;
        return PNG_OUT_OF_MEMORY_ERROR;
    }


    /* set the individual row_pointers to point at the correct offsets */

    for (i = 0;  i < mainprog_ptr->height;  ++i)
        mainprog_ptr->row_pointers[i] = mainprog_ptr->rgba_data + i*rowbytes;


    /* now we can go ahead and just read the whole image */

    png_read_image(png_ptr, (png_bytepp)mainprog_ptr->row_pointers);


    /* and we're done!  (png_read_end() can be omitted if no processing of
     * post-IDAT text/time/etc. is desired) */

    png_read_end(png_ptr, NULL);

    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    return SUCCESS;
}





/*
   retval:
     0 = success
    34 = insufficient memory
    35 = libpng error (via longjmp())
 */

pngquant_error rwpng_write_image_init(FILE *outfile, read_info *mainprog_ptr)
{
    png_structp png_ptr;       /* note:  temporary variables! */
    png_infop info_ptr;


    /* could also replace libpng warning-handler (final NULL), but no need: */

    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, mainprog_ptr,
      rwpng_error_handler, NULL);
    if (!png_ptr) {
        return INIT_OUT_OF_MEMORY_ERROR;   /* out of memory */
    }
    mainprog_ptr->png_ptr = png_ptr;

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_write_struct(&png_ptr, NULL);
        return INIT_OUT_OF_MEMORY_ERROR;   /* out of memory */
    }


    /* setjmp() must be called in every function that calls a PNG-writing
     * libpng function, unless an alternate error handler was installed--
     * but compatible error handlers must either use longjmp() themselves
     * (as in this program) or exit immediately, so here we go: */

    if (setjmp(mainprog_ptr->jmpbuf)) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        return LIBPNG_INIT_ERROR;   /* libpng error (via longjmp()) */
    }


    /* make sure outfile is (re)opened in BINARY mode */

    png_init_io(png_ptr, outfile);


    /* set the compression levels--in general, always want to leave filtering
     * turned on (except for palette images) and allow all of the filters,
     * which is the default; want 32K zlib window, unless entire image buffer
     * is 16K or smaller (unknown here)--also the default; usually want max
     * compression (NOT the default); and remaining compression flags should
     * be left alone */

    png_set_compression_level(png_ptr, Z_BEST_COMPRESSION);

    png_set_IHDR(png_ptr, info_ptr, mainprog_ptr->width, mainprog_ptr->height,
      8, PNG_COLOR_TYPE_RGB_ALPHA,
      mainprog_ptr->interlaced, PNG_COMPRESSION_TYPE_DEFAULT,
      PNG_FILTER_TYPE_DEFAULT);

    if (mainprog_ptr->gamma > 0.0)
        png_set_gAMA(png_ptr, info_ptr, mainprog_ptr->gamma);

    // disable filtering as posterization aims to optimize unchanged values
    png_set_filter(png_ptr, PNG_FILTER_TYPE_BASE, PNG_FILTER_VALUE_NONE);

    /* write all chunks up to (but not including) first IDAT */
    png_write_info(png_ptr, info_ptr);

    /* if we wanted to write any more text info *after* the image data, we
     * would set up text struct(s) here and call png_set_text() again, with
     * just the new data; png_set_tIME() could also go here, but it would
     * have no effect since we already called it above (only one tIME chunk
     * allowed) */


    /* set up the transformations:  for now, just pack low-bit-depth pixels
     * into bytes (one, two or four pixels per byte) */

    png_set_packing(png_ptr);

    /* make sure we save our pointers for use in writepng_encode_image() */

    mainprog_ptr->png_ptr = png_ptr;
    mainprog_ptr->info_ptr = info_ptr;

    /* OK, that's all we need to do for now; return happy */
    return SUCCESS;
}






/* returns 0 for success, 45 for libpng (longjmp) problem */

pngquant_error rwpng_write_image_whole(read_info *mainprog_ptr)
{
    png_structp png_ptr = (png_structp)mainprog_ptr->png_ptr;
    png_infop info_ptr = (png_infop)mainprog_ptr->info_ptr;

    /* as always, setjmp() must be called in every function that calls a
     * PNG-writing libpng function */

    if (setjmp(mainprog_ptr->jmpbuf)) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        mainprog_ptr->png_ptr = NULL;
        mainprog_ptr->info_ptr = NULL;
        return LIBPNG_WRITE_WHOLE_ERROR; /* libpng error (via longjmp()) */
    }


    png_write_flush(mainprog_ptr->png_ptr);

    /* and now we just write the whole image; libpng takes care of interlacing
     * for us */
    png_write_image(png_ptr, mainprog_ptr->row_pointers);


    /* since that's it, we also close out the end of the PNG file now--if we
     * had any text or time info to write after the IDATs, second argument
     * would be info_ptr, but we optimize slightly by sending NULL pointer: */

    png_write_end(png_ptr, NULL);

    png_destroy_write_struct(&png_ptr, &info_ptr);
    mainprog_ptr->png_ptr = NULL;
    mainprog_ptr->info_ptr = NULL;

    return SUCCESS;
}




static void rwpng_error_handler(png_structp png_ptr, png_const_charp msg)
{
    read_or_write_info  *mainprog_ptr;

    /* This function, aside from the extra step of retrieving the "error
     * pointer" (below) and the fact that it exists within the application
     * rather than within libpng, is essentially identical to libpng's
     * default error handler.  The second point is critical:  since both
     * setjmp() and longjmp() are called from the same code, they are
     * guaranteed to have compatible notions of how big a jmp_buf is,
     * regardless of whether _BSD_SOURCE or anything else has (or has not)
     * been defined. */

    fprintf(stderr, "rwpng libpng error: %s\n", msg);
    fflush(stderr);

    mainprog_ptr = png_get_error_ptr(png_ptr);
    if (mainprog_ptr == NULL) {         /* we are completely hosed now */
        fprintf(stderr,
          "rwpng severe error:  jmpbuf not recoverable; terminating.\n");
        fflush(stderr);
        exit(99);
    }

    longjmp(mainprog_ptr->jmpbuf, 1);
}
