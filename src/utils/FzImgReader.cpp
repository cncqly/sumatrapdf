/* Copyright 2014 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#ifndef NO_LIBMUPDF

extern "C" {
#include <mupdf/fitz.h>
}

#include "FzImgReader.h"

// interaction between '_setjmp' and C++ object destruction is non-portable
#pragma warning(disable: 4611)

using namespace Gdiplus;

namespace fitz {

static Bitmap *ImageFromJpegData(fz_context *ctx, const char *data, int len)
{
    int w, h, xres, yres;
    fz_colorspace *cs = NULL;
    fz_stream *stm = NULL;

    fz_var(cs);
    fz_var(stm);

    fz_try(ctx) {
        fz_load_jpeg_info(ctx, (unsigned char *)data, len, &w, &h, &xres, &yres, &cs);
        stm = fz_open_memory(ctx, (unsigned char *)data, len);
        stm = fz_open_dctd(stm, -1, 0, NULL);
    }
    fz_catch(ctx) {
        fz_drop_colorspace(ctx, cs);
        cs = NULL;
    }

    PixelFormat fmt = fz_device_rgb(ctx) == cs ? PixelFormat32bppARGB :
                      fz_device_gray(ctx) == cs ? PixelFormat32bppARGB :
                      fz_device_cmyk(ctx) == cs ? PixelFormat32bppCMYK :
                      PixelFormatUndefined;
    if (PixelFormatUndefined == fmt) {
        fz_close(stm);
        fz_drop_colorspace(ctx, cs);
        return NULL;
    }

    Bitmap bmp(w, h, fmt);
    bmp.SetResolution(xres, yres);

    Rect bmpRect(0, 0, w, h);
    BitmapData bmpData;
    Status ok = bmp.LockBits(&bmpRect, ImageLockModeWrite, fmt, &bmpData);
    if (ok != Ok) {
        fz_close(stm);
        fz_drop_colorspace(ctx, cs);
        return NULL;
    }

    fz_var(bmp);
    fz_var(bmpRect);

    fz_try(ctx) {
        for (int y = 0; y < h; y++) {
            unsigned char *data = (unsigned char *)bmpData.Scan0 + y * bmpData.Stride;
            for (int x = 0; x < w * 4; x += 4) {
                int len = fz_read(stm, data + x, cs->n);
                if (len != cs->n)
                    fz_throw(ctx, FZ_ERROR_GENERIC, "insufficient data for image");
                if (3 == cs->n) { // RGB -> BGRA
                    Swap(data[x], data[x + 2]);
                    data[x + 3] = 0xFF;
                }
                else if (1 == cs->n) { // gray -> BGRA
                    data[x + 1] = data[x + 2] = data[x];
                    data[x + 3] = 0xFF;
                }
                else if (4 == cs->n) { // CMYK color inversion
                    for (int k = 0; k < 4; k++)
                        data[x + k] = 255 - data[x + k];
                }
            }
        }
    }
    fz_always(ctx) {
        bmp.UnlockBits(&bmpData);
        fz_close(stm);
        fz_drop_colorspace(ctx, cs);
    }
    fz_catch(ctx) {
        return NULL;
    }

    // hack to avoid the use of ::new (because there won't be a corresponding ::delete)
    return bmp.Clone(0, 0, w, h, fmt);
}

Bitmap *ImageFromData(const char *data, size_t len)
{
    if (len > INT_MAX || len < 8)
        return NULL;

    fz_context *ctx = fz_new_context(NULL, NULL, 0);
    if (!ctx)
        return NULL;

    Bitmap *result = NULL;
    if (str::StartsWith(data, "\xFF\xD8"))
        result = ImageFromJpegData(ctx, data, (int)len);

    fz_free_context(ctx);

    return result;
}

}

#else

#include "FzImgReader.h"

namespace fitz {
Gdiplus::Bitmap *ImageFromData(const char *data, size_t len) { return NULL; }
}

#endif
