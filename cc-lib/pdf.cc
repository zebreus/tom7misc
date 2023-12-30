// PDF output, based on Andre Renaud's public domain PDFGen:
//   https://github.com/AndreRenaud/PDFGen/tree/master
//
// Local changes:
//   - "fixed" some printf format parameter warnings
//   - Compile as C++

#include "pdf.h"

#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS 1 // Drop the MSVC complaints about snprintf
#define _USE_MATH_DEFINES
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#else

#ifndef _POSIX_SOURCE
#define _POSIX_SOURCE /* For localtime_r */
#endif

// TODO: Use c++ constant
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600 /* for M_SQRT2 */
#endif

#include <sys/types.h> /* for ssize_t */
#endif

#include <cstdint>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "base/logging.h"
#include "base/stringprintf.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define PDF_RGB_R(c) (float)((((c) >> 16) & 0xff) / 255.0)
#define PDF_RGB_G(c) (float)((((c) >> 8) & 0xff) / 255.0)
#define PDF_RGB_B(c) (float)((((c) >> 0) & 0xff) / 255.0)
#define PDF_IS_TRANSPARENT(c) (((c) >> 24) == 0xff)

#if defined(_MSC_VER)
#define inline __inline
#define snprintf _snprintf
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define fileno _fileno
#define fstat _fstat
#ifdef stat
#undef stat
#endif
#define stat _stat
#define SKIP_ATTRIBUTE
#else
#include <strings.h> // strcasecmp
#endif

// TODO: Use std::endian
/**
 * Try and support big & little endian machines
 */
static inline uint32_t bswap32(uint32_t x)
{
    return (((x & 0xff000000u) >> 24) | ((x & 0x00ff0000u) >> 8) |
            ((x & 0x0000ff00u) << 8) | ((x & 0x000000ffu) << 24));
}

#ifdef __has_include // C++17, supported as extension to C++11 in clang, GCC
                     // 5+, vs2015
#if __has_include(<endian.h>)
#include <endian.h> // gnu libc normally provides, linux
#elif __has_include(<machine/endian.h>)
#include <machine/endian.h> //open bsd, macos
#elif __has_include(<sys/param.h>)
#include <sys/param.h> // mingw, some bsd (not open/macos)
#elif __has_include(<sys/isadefs.h>)
#include <sys/isadefs.h> // solaris
#endif
#endif

#if !defined(__LITTLE_ENDIAN__) && !defined(__BIG_ENDIAN__)
#ifndef __BYTE_ORDER__
/* Fall back to little endian by default */
#define __LITTLE_ENDIAN__
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__ || __BYTE_ORDER == __BIG_ENDIAN
#define __BIG_ENDIAN__
#else
#define __LITTLE_ENDIAN__
#endif
#endif

#if defined(__LITTLE_ENDIAN__)
#define ntoh32(x) bswap32((x))
#elif defined(__BIG_ENDIAN__)
#define ntoh32(x) (x)
#endif

// Limits on image sizes for sanity checking & to avoid plausible overflow
// issues
static constexpr int MAX_IMAGE_WIDTH = (16 * 1024);
static constexpr int MAX_IMAGE_HEIGHT = (16 * 1024);

// Signatures for various image formats
static const uint8_t bmp_signature[] = {'B', 'M'};
static const uint8_t png_signature[] = {0x89, 0x50, 0x4E, 0x47,
                                        0x0D, 0x0A, 0x1A, 0x0A};
static const uint8_t jpeg_signature[] = {0xff, 0xd8};
static const uint8_t ppm_signature[] = {'P', '6'};
static const uint8_t pgm_signature[] = {'P', '5'};

// Special signatures for PNG chunks
static const char png_chunk_header[] = "IHDR";
static const char png_chunk_palette[] = "PLTE";
static const char png_chunk_data[] = "IDAT";
static const char png_chunk_end[] = "IEND";


// XXX use standard stuff
#pragma pack(push, 1)
struct png_chunk {
    uint32_t length;
    // chunk type, see png_chunk_header, png_chunk_data, png_chunk_end
    char type[4];
};
#pragma pack(pop)

const char *PDF::ObjTypeName(ObjType t) {
  switch (t) {
  case OBJ_none: return "none";
  case OBJ_info: return "info";
  case OBJ_stream: return "stream";
  case OBJ_font: return "font";
  case OBJ_page: return "page";
  case OBJ_bookmark: return "bookmark";
  case OBJ_outline: return "outline";
  case OBJ_catalog: return "catalog";
  case OBJ_pages: return "pages";
  case OBJ_image: return "image";
  case OBJ_link: return "link";
  default: break;
  }
  return "???";
}

// Simple data container to store a single 24 Bit RGB value, used for
// processing PNG images
struct rgb_value {
    uint8_t red;
    uint8_t blue;
    uint8_t green;
};

// Locales can replace the decimal character with a ','.
// This breaks the PDF output, so we force a 'safe' locale.
static void force_locale(char *buf, int len)
{
    char *saved_locale = setlocale(LC_ALL, nullptr);

    if (!saved_locale) {
        *buf = '\0';
    } else {
        strncpy(buf, saved_locale, len - 1);
        buf[len - 1] = '\0';
    }

    setlocale(LC_NUMERIC, "POSIX");
}

static void restore_locale(char *buf)
{
    setlocale(LC_ALL, buf);
}

/**
 * PDF Implementation
 */

// XXX just use StringPrintf.
int PDF::SetErr(int errval, const char *buffer, ...) {
  va_list ap;
  int len;

  va_start(ap, buffer);
  len = vsnprintf(errstr, sizeof(errstr) - 1, buffer, ap);
  va_end(ap);

  if (len < 0) {
    errstr[0] = '\0';
    return errval;
  }

  if (len >= (int)(sizeof(errstr) - 1))
    len = (int)(sizeof(errstr) - 1);

  errstr[len] = '\0';
  errval = errval;

  return errval;
}

std::string PDF::GetErr() const {
  return errstr;
}

void PDF::ClearErr() {
  errstr[0] = '\0';
  errval = 0;
}

int PDF::GetErrCode() const {
  return errval;
}

PDF::Object *PDF::pdf_get_object(int index) {
  return objects[index];
}

void PDF::pdf_append_object(Object *obj) {
  int index = (int)objects.size();
  objects.push_back(obj);

  CHECK(index >= 0);

  obj->index = index;

  if (last_objects[obj->type]) {
    obj->prev = last_objects[obj->type];
    last_objects[obj->type]->next = obj;
  }
  last_objects[obj->type] = obj;

  if (!first_objects[obj->type])
    first_objects[obj->type] = obj;
}

void PDF::pdf_object_destroy(Object *object) {
  delete object;
}

PDF::Object *PDF::pdf_add_object_internal(Object *obj) {
  CHECK(obj != nullptr);
  pdf_append_object(obj);
  return obj;
}

void PDF::pdf_del_object(Object *obj) {
  const ObjType type = obj->type;
  CHECK(obj->index >= 0 && obj->index < (int)objects.size());
  objects[obj->index] = nullptr;

  if (last_objects[type] == obj) {
    last_objects[type] = nullptr;
    for (Object *o : objects) {
      if (o != nullptr && o->type == type) {
        last_objects[type] = o;
      }
    }
  }

  if (first_objects[type] == obj) {
    first_objects[type] = nullptr;
      for (Object *o : objects) {
      if (o && o->type == type) {
        first_objects[type] = o;
        break;
      }
    }
  }

  pdf_object_destroy(obj);
}

PDF::PDF(float width, float height,
         const std::optional<PDF::Info> &info) :
  width(width), height(height) {

  /* We don't want to use ID 0 */
  (void)AddObject(new None);

  /* Create the 'info' object */
  InfoObj *obj = AddObject(new InfoObj);
  CHECK(obj != nullptr);

  if (info.has_value()) {
    obj->info = info.value();
    obj->info.creator[sizeof(obj->info.creator) - 1] = '\0';
    obj->info.producer[sizeof(obj->info.producer) - 1] = '\0';
    obj->info.title[sizeof(obj->info.title) - 1] = '\0';
    obj->info.author[sizeof(obj->info.author) - 1] = '\0';
    obj->info.subject[sizeof(obj->info.subject) - 1] = '\0';
    obj->info.date[sizeof(obj->info.date) - 1] = '\0';
  }
  /* FIXME: Should be quoting PDF strings? */
  if (!obj->info.date[0]) {
    time_t now = time(nullptr);
    struct tm tm;
#ifdef _WIN32
    struct tm *tmp;
    tmp = localtime(&now);
    tm = *tmp;
#else
    localtime_r(&now, &tm);
#endif
    strftime(obj->info.date, sizeof(obj->info.date), "%Y%m%d%H%M%SZ",
             &tm);
  }

  CHECK(AddObject(new Pages) != nullptr);
  CHECK(AddObject(new Catalog) != nullptr);
  CHECK(SetFont("Times-Roman") >= 0);
}

float PDF::Width() const { return width; }
float PDF::Height() const { return height; }

PDF::~PDF() {
  for (Object *obj : objects)
    pdf_object_destroy(obj);
  objects.clear();
}

PDF::Object *PDF::pdf_find_first_object(int type) {
  return first_objects[type];
}

PDF::Object *PDF::pdf_find_last_object(int type) {
  return last_objects[type];
}

int PDF::SetFont(const char *font) {
  int last_index = 0;

  /* See if we've used this font before */
  FontObj *fobj = nullptr;
  for (Object *obj = pdf_find_first_object(OBJ_font); obj; obj = obj->next) {
    fobj = (FontObj*)obj;
    if (strcmp(fobj->name, font) == 0)
      break;
    last_index = fobj->font_index;
  }

  /* Create a new font object if we need it */
  if (!fobj) {
    fobj = AddObject(new FontObj);
    CHECK(fobj);
    // XXX just use string
    strncpy(fobj->name, font, sizeof(fobj->name) - 1);
    fobj->name[sizeof(fobj->name) - 1] = '\0';
    fobj->font_index = last_index + 1;
  }

  current_font = fobj;

  return 0;
}

PDF::Page *PDF::AppendNewPage() {
  Page *page = AddObject(new Page);

  if (!page)
    return nullptr;

  page->width = this->width;
  page->height = this->height;

  return page;
}

PDF::Page *PDF::GetPage(int page_number) {
  if (page_number <= 0) {
    SetErr(-EINVAL, "page number must be >= 1");
    return nullptr;
  }

  for (Object *obj = pdf_find_first_object(OBJ_page); obj;
       obj = obj->next, page_number--) {
    if (page_number == 1) {
      return (Page*)obj;
    }
  }

  SetErr(-EINVAL, "no such page");
  return nullptr;
}

void PDF::Page::SetSize(float w, float h) {
  width = w;
  height = h;
}

// Recursively scan for the number of children
int PDF::pdf_get_bookmark_count(const Object *obj) {
  int count = 0;
  if (obj->type == OBJ_bookmark) {
    Bookmark *bobj = (Bookmark*)obj;
    int nchildren = (int)bobj->children.size();
    count += nchildren;
    for (int i = 0; i < nchildren; i++) {
      count += pdf_get_bookmark_count(bobj->children[i]);
    }
  }
  return count;
}

int PDF::pdf_save_object(FILE *fp, int index) {
  Object *object = pdf_get_object(index);
  if (!object)
    return -ENOENT;

  printf("Save object %p %d type %d=%s\n",
         fp, index, object->type,
         ObjTypeName(object->type));

  if (object->type == OBJ_none)
    return -ENOENT;

  object->offset = ftell(fp);

  fprintf(fp, "%d 0 obj\r\n", index);

  switch (object->type) {
  case OBJ_stream: {
    Stream *sobj = (Stream*)object;
    fwrite(sobj->stream.data(),
           sobj->stream.size(),
           1, fp);
    break;
  }

  case OBJ_image: {
    Image *iobj = (Image*)object;
    fwrite(iobj->stream.data(),
           iobj->stream.size(),
           1, fp);
    break;
  }

  case OBJ_info: {
    const InfoObj *iobj = (InfoObj*)object;
    const PDF::Info *info = &iobj->info;

    fprintf(fp, "<<\r\n");
    if (info->creator[0])
      fprintf(fp, "  /Creator (%s)\r\n", info->creator);
    if (info->producer[0])
      fprintf(fp, "  /Producer (%s)\r\n", info->producer);
    if (info->title[0])
      fprintf(fp, "  /Title (%s)\r\n", info->title);
    if (info->author[0])
      fprintf(fp, "  /Author (%s)\r\n", info->author);
    if (info->subject[0])
      fprintf(fp, "  /Subject (%s)\r\n", info->subject);
    if (info->date[0])
      fprintf(fp, "  /CreationDate (D:%s)\r\n", info->date);
    fprintf(fp, ">>\r\n");
    break;
  }

  case OBJ_page: {
    Object *pages = pdf_find_first_object(OBJ_pages);
    bool printed_xobjects = false;

    Page *pobj = (Page*)object;

    fprintf(fp,
            "<<\r\n"
            "  /Type /Page\r\n"
            "  /Parent %d 0 R\r\n",
            pages->index);
    fprintf(fp, "  /MediaBox [0 0 %f %f]\r\n",
            pobj->width,
            pobj->height);
    fprintf(fp, "  /Resources <<\r\n");
    fprintf(fp, "    /Font <<\r\n");
    for (Object *font = pdf_find_first_object(OBJ_font);
         font; font = font->next) {
      const FontObj *fobj = (FontObj*)font;
      fprintf(fp, "      /F%d %d 0 R\r\n",
              fobj->font_index,
              font->index);
    }
    fprintf(fp, "    >>\r\n");
    // We trim transparency to just 4-bits
    fprintf(fp, "    /ExtGState <<\r\n");
    for (int i = 0; i < 16; i++) {
      fprintf(fp, "      /GS%d <</ca %f>>\r\n", i,
              (float)(15 - i) / 15);
    }
    fprintf(fp, "    >>\r\n");

    for (Object *image = pdf_find_first_object(OBJ_image);
         image; image = image->next) {
      Stream *iobj = (Stream*)image;
      if (iobj->page == object) {
        if (!printed_xobjects) {
          fprintf(fp, "    /XObject <<");
          printed_xobjects = true;
        }
        fprintf(fp, "      /Image%d %d 0 R ",
                image->index,
                image->index);
      }
    }
    if (printed_xobjects)
      fprintf(fp, "    >>\r\n");
    fprintf(fp, "  >>\r\n");

    fprintf(fp, "  /Contents [\r\n");
    for (Object *child : pobj->children) {
      fprintf(fp, "%d 0 R\r\n", child->index);
    }
    fprintf(fp, "]\r\n");

    if (!pobj->annotations.empty()) {
      fprintf(fp, "  /Annots [\r\n");
      for (Object *child : pobj->annotations) {
        fprintf(fp, "%d 0 R\r\n", child->index);
      }
      fprintf(fp, "]\r\n");
    }

    fprintf(fp, ">>\r\n");
    break;
  }

  case OBJ_bookmark: {
    Bookmark *bobj = (Bookmark *)object;

    Object *parent = bobj->parent;
    if (!parent)
      parent = pdf_find_first_object(OBJ_outline);
    if (!bobj->page)
      break;
    fprintf(fp,
            "<<\r\n"
            "  /Dest [%d 0 R /XYZ 0 %f null]\r\n"
            "  /Parent %d 0 R\r\n"
            "  /Title (%s)\r\n",
            bobj->page->index,
            this->height,
            parent->index,
            bobj->name);
    int nchildren = (int)bobj->children.size();
    if (nchildren > 0) {
      Object *f = (Object *)bobj->children[0];
      Object *l = (Object *)bobj->children[nchildren - 1];
      fprintf(fp, "  /First %d 0 R\r\n", f->index);
      fprintf(fp, "  /Last %d 0 R\r\n", l->index);
      fprintf(fp, "  /Count %d\r\n", pdf_get_bookmark_count(object));
    }

    {
      // Find the previous bookmark with the same parent
      Bookmark *other = (Bookmark*)object->prev;
      while (other && other->parent != bobj->parent) {
        other = (Bookmark*)other->prev;
      }

      if (other != nullptr) {
        fprintf(fp, "  /Prev %d 0 R\r\n", other->index);
      }
    }

    {
      // Find the next bookmark with the same parent
      Bookmark *other = (Bookmark*)object->next;
      while (other && other->parent != bobj->parent) {
        other = (Bookmark*)other->next;
      }

      if (other != nullptr) {
        fprintf(fp, "  /Next %d 0 R\r\n", other->index);
      }
    }

    fprintf(fp, ">>\r\n");
    break;
  }

  case OBJ_outline: {
    Object *first = pdf_find_first_object(OBJ_bookmark);
    Object *last = pdf_find_last_object(OBJ_bookmark);

    if (first && last) {
      int count = 0;
      Bookmark *cur = (Bookmark*)first;
      while (cur) {
        if (!cur->parent) {
          count += pdf_get_bookmark_count(cur) + 1;
        }
        cur = (Bookmark*)cur->next;
      }

      /* Bookmark outline */
      fprintf(fp,
              "<<\r\n"
              "  /Count %d\r\n"
              "  /Type /Outlines\r\n"
              "  /First %d 0 R\r\n"
              "  /Last %d 0 R\r\n"
              ">>\r\n",
              count, first->index, last->index);
    }
    break;
  }

  case OBJ_font: {
    FontObj *fobj = (FontObj*)object;
    fprintf(fp,
            "<<\r\n"
            "  /Type /Font\r\n"
            "  /Subtype /Type1\r\n"
            "  /BaseFont /%s\r\n"
            "  /Encoding /WinAnsiEncoding\r\n"
            ">>\r\n",
            fobj->name);
    break;
  }

  case OBJ_pages: {
    int npages = 0;

    fprintf(fp, "<<\r\n"
            "  /Type /Pages\r\n"
            "  /Kids [ ");
    for (Object *page = pdf_find_first_object(OBJ_page);
         page; page = page->next) {
      npages++;
      fprintf(fp, "%d 0 R ", page->index);
    }
    fprintf(fp, "]\r\n");
    fprintf(fp, "  /Count %d\r\n", npages);
    fprintf(fp, ">>\r\n");
    break;
  }

  case OBJ_catalog: {
    Object *outline = pdf_find_first_object(OBJ_outline);
    Object *pages = pdf_find_first_object(OBJ_pages);

    fprintf(fp, "<<\r\n"
            "  /Type /Catalog\r\n");
    if (outline)
      fprintf(fp,
              "  /Outlines %d 0 R\r\n"
              "  /PageMode /UseOutlines\r\n",
              outline->index);
    fprintf(fp,
            "  /Pages %d 0 R\r\n"
            ">>\r\n",
            pages->index);
    break;
  }

  case OBJ_link: {
    Link *lobj = (Link*)object;
    fprintf(fp,
            "<<\r\n"
            "  /Type /Annot\r\n"
            "  /Subtype /Link\r\n"
            "  /Rect [%f %f %f %f]\r\n"
            "  /Dest [%u 0 R /XYZ %f %f null]\r\n"
            "  /Border [0 0 0]\r\n"
            ">>\r\n",
            lobj->llx, lobj->lly, lobj->urx,
            lobj->ury, lobj->target_page->index,
            lobj->target_x, lobj->target_y);
    break;
  }

  default:
    return SetErr(-EINVAL, "Invalid PDF object type %d",
                  object->type);
  }

  fprintf(fp, "endobj\r\n");

  return 0;
}

// Slightly modified djb2 hash algorithm to get pseudo-random ID
static uint64_t hash(uint64_t hash, const void *data, size_t len)
{
    const uint8_t *d8 = (const uint8_t *)data;
    for (; len; len--) {
        hash = (((hash & 0x03ffffffffffffff) << 5) +
                (hash & 0x7fffffffffffffff)) +
               *d8++;
    }
    return hash;
}

int PDF::pdf_save_file(FILE *fp) {
  int xref_offset;
  int xref_count = 0;
  uint64_t id1, id2;
  time_t now = time(nullptr);
  char saved_locale[32];

  force_locale(saved_locale, sizeof(saved_locale));

  fprintf(fp, "%%PDF-1.3\r\n");
  /* Hibit bytes */
  fprintf(fp, "%c%c%c%c%c\r\n", 0x25, 0xc7, 0xec, 0x8f, 0xa2);

  /* Dump all the objects & get their file offsets */
  for (int i = 0; i < (int)objects.size(); i++)
    if (pdf_save_object(fp, i) >= 0)
      xref_count++;

  /* xref */
  xref_offset = ftell(fp);
  fprintf(fp, "xref\r\n");
  fprintf(fp, "0 %d\r\n", xref_count + 1);
  fprintf(fp, "0000000000 65535 f\r\n");
  for (Object *obj : objects) {
    if (obj->type != OBJ_none) {
      fprintf(fp, "%10.10d 00000 n\r\n", obj->offset);
    }
  }

  fprintf(fp,
          "trailer\r\n"
          "<<\r\n"
          "/Size %d\r\n",
          xref_count + 1);
  Object *obj = pdf_find_first_object(OBJ_catalog);
  CHECK(obj != nullptr);
  fprintf(fp, "/Root %d 0 R\r\n", obj->index);

  const InfoObj *iobj = (InfoObj*)pdf_find_first_object(OBJ_info);
  fprintf(fp, "/Info %d 0 R\r\n", iobj->index);
  /* Generate document unique IDs */
  id1 = hash(5381, &iobj->info, sizeof (PDF::Info));
  id1 = hash(id1, &xref_count, sizeof (xref_count));
  id2 = hash(5381, &now, sizeof(now));
  fprintf(fp, "/ID [<%16.16" PRIx64 "> <%16.16" PRIx64 ">]\r\n", id1, id2);
  fprintf(fp, ">>\r\n"
          "startxref\r\n");
  fprintf(fp, "%d\r\n", xref_offset);
  fprintf(fp, "%%%%EOF\r\n");

  restore_locale(saved_locale);

  return 0;
}

bool PDF::Save(const char *filename) {
  FILE *fp = nullptr;

  if (filename == nullptr)
    fp = stdout;
  else if ((fp = fopen(filename, "wb")) == nullptr) {
    SetErr(-errno, "Unable to open '%s': %s", filename,
           strerror(errno));
    return false;
  }

  int e = pdf_save_file(fp);

  if (fp != stdout) {
    if (fclose(fp) != 0 && e >= 0) {
      SetErr(-errno, "Unable to close '%s': %s",
             filename, strerror(errno));
      return false;
    }
  }

  return true;
}

int PDF::pdf_add_stream(Page *page, const char *buffer) {
  if (!page)
    page = (Page*)pdf_find_last_object(OBJ_page);

  if (!page)
    return SetErr(-EINVAL, "Invalid pdf page");

  size_t len = strlen(buffer);
  /* We don't want any trailing whitespace in the stream */
  while (len >= 1 && (buffer[len - 1] == '\r' || buffer[len - 1] == '\n'))
    len--;

  Stream *sobj = AddObject(new Stream);
  if (!sobj)
    return errval;

  sobj->stream = StringPrintf("<< /Length %d >>stream\r\n", (int)len);
  sobj->stream.append(buffer, len);
  StringAppendF(&sobj->stream, "\r\nendstream\r\n");

  int index = (int)page->children.size();
  page->children.push_back(sobj);
  return index;
}

#if 0
int pdf_add_bookmark(pdf_doc *pdf, Object *page, int parent,
                     const char *name)
{
    Object *obj, *outline = nullptr;

    if (!page)
        page = pdf_find_last_object(pdf, OBJ_page);

    if (!page)
        return SetErr(-EINVAL,
                           "Unable to add bookmark, no pages available");

    if (!pdf_find_first_object(pdf, OBJ_outline)) {
        outline = pdf_add_object(pdf, OBJ_outline);
        if (!outline)
            return pdf->errval;
    }

    obj = pdf_add_object(pdf, OBJ_bookmark);
    if (!obj) {
        if (outline)
            pdf_del_object(pdf, outline);
        return pdf->errval;
    }

    strncpy(obj->bookmark.name, name, sizeof(obj->bookmark.name) - 1);
    obj->bookmark.name[sizeof(obj->bookmark.name) - 1] = '\0';
    obj->bookmark.page = page;
    if (parent >= 0) {
        Object *parent_obj = pdf_get_object(pdf, parent);
        if (!parent_obj)
            return SetErr(-EINVAL, "Invalid parent ID %d supplied",
                               parent);
        obj->bookmark.parent = parent_obj;
        flexarray_append(&parent_obj->bookmark.children, obj);
    }

    return obj->index;
}

int pdf_add_link(pdf_doc *pdf, Object *page, float x,
                 float y, float width, float height,
                 Object *target_page, float target_x,
                 float target_y)
{
    Object *obj;

    if (!page)
        page = pdf_find_last_object(pdf, OBJ_page);

    if (!page)
        return SetErr(-EINVAL,
                           "Unable to add link, no pages available");

    if (!target_page)
        return SetErr(-EINVAL, "Unable to link, no target page");

    obj = pdf_add_object(pdf, OBJ_link);
    if (!obj) {
        return pdf->errval;
    }

    obj->link.target_page = target_page;
    obj->link.target_x = target_x;
    obj->link.target_y = target_y;
    obj->link.llx = x;
    obj->link.lly = y;
    obj->link.urx = x + width;
    obj->link.ury = y + height;
    flexarray_append(&page->page.annotations, obj);

    return obj->index;
}
#endif

static int utf8_to_utf32(const char *utf8, int len, uint32_t *utf32)
{
    uint32_t ch;
    uint8_t mask;

    if (len <= 0 || !utf8 || !utf32)
        return -EINVAL;

    ch = *(uint8_t *)utf8;
    if ((ch & 0x80) == 0) {
        len = 1;
        mask = 0x7f;
    } else if ((ch & 0xe0) == 0xc0 && len >= 2) {
        len = 2;
        mask = 0x1f;
    } else if ((ch & 0xf0) == 0xe0 && len >= 3) {
        len = 3;
        mask = 0xf;
    } else if ((ch & 0xf8) == 0xf0 && len >= 4) {
        len = 4;
        mask = 0x7;
    } else
        return -EINVAL;

    ch = 0;
    for (int i = 0; i < len; i++) {
        int shift = (len - i - 1) * 6;
        if (!*utf8)
            return -EINVAL;
        if (i == 0)
            ch |= ((uint32_t)(*utf8++) & mask) << shift;
        else
            ch |= ((uint32_t)(*utf8++) & 0x3f) << shift;
    }

    *utf32 = ch;

    return len;
}

[[maybe_unused]]
static int utf8_to_pdfencoding(const char *utf8, int len,
                               uint8_t *res) {
  uint32_t code;
  *res = 0;

  int code_len = utf8_to_utf32(utf8, len, &code);
  CHECK(code_len >= 0) << "Invalid UTF-8 encoding";

  if (code > 255) {
    /* We support *some* minimal UTF-8 characters */
    // See Appendix D of
    // https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.7old.pdf
    // These are all in WinAnsiEncoding
    switch (code) {
    case 0x152: // Latin Capital Ligature OE
      *res = 0214;
      break;
    case 0x153: // Latin Small Ligature oe
      *res = 0234;
      break;
    case 0x160: // Latin Capital Letter S with caron
      *res = 0212;
      break;
    case 0x161: // Latin Small Letter S with caron
      *res = 0232;
      break;
    case 0x178: // Latin Capital Letter y with diaeresis
      *res = 0237;
      break;
    case 0x17d: // Latin Capital Letter Z with caron
      *res = 0216;
      break;
    case 0x17e: // Latin Small Letter Z with caron
      *res = 0236;
      break;
    case 0x192: // Latin Small Letter F with hook
      *res = 0203;
      break;
    case 0x2c6: // Modifier Letter Circumflex Accent
      *res = 0210;
      break;
    case 0x2dc: // Small Tilde
      *res = 0230;
      break;
    case 0x2013: // Endash
      *res = 0226;
      break;
    case 0x2014: // Emdash
      *res = 0227;
      break;
    case 0x2018: // Left Single Quote
      *res = 0221;
      break;
    case 0x2019: // Right Single Quote
      *res = 0222;
      break;
    case 0x201a: // Single low-9 Quotation Mark
      *res = 0202;
      break;
    case 0x201c: // Left Double Quote
      *res = 0223;
      break;
    case 0x201d: // Right Double Quote
      *res = 0224;
      break;
    case 0x201e: // Double low-9 Quotation Mark
      *res = 0204;
      break;
    case 0x2020: // Dagger
      *res = 0206;
      break;
    case 0x2021: // Double Dagger
      *res = 0207;
      break;
    case 0x2022: // Bullet
      *res = 0225;
      break;
    case 0x2026: // Horizontal Ellipsis
      *res = 0205;
      break;
    case 0x2030: // Per Mille Sign
      *res = 0211;
      break;
    case 0x2039: // Single Left-pointing Angle Quotation Mark
      *res = 0213;
      break;
    case 0x203a: // Single Right-pointing Angle Quotation Mark
      *res = 0233;
      break;
    case 0x20ac: // Euro
      *res = 0200;
      break;
    case 0x2122: // Trade Mark Sign
      *res = 0231;
      break;
    default:
      CHECK(false) <<
        StringPrintf("Unsupported UTF-8 character: 0x%x 0o%o %s",
                     code, code, utf8);
    }
  } else {
    *res = code;
  }
  return code_len;
}

#if 0

static int pdf_add_text_spacing(pdf_doc *pdf, Object *page,
                                const char *text, float size, float xoff,
                                float yoff, uint32_t colour, float spacing,
                                float angle)
{
    int ret;
    size_t len = text ? strlen(text) : 0;
    dstr str = INIT_DSTR;
    int alpha = (colour >> 24) >> 4;

    /* Don't bother adding empty/null strings */
    if (!len)
        return 0;

    dstr_append(&str, "BT ");
    dstr_printf(&str, "/GS%d gs ", alpha);
    if (angle != 0) {
        dstr_printf(&str, "%f %f %f %f %f %f Tm ", cosf(angle), sinf(angle),
                    -sinf(angle), cosf(angle), xoff, yoff);
    } else {
        dstr_printf(&str, "%f %f TD ", xoff, yoff);
    }
    dstr_printf(&str, "/F%d %f Tf ", pdf->current_font->font.font_index, size);
    dstr_printf(&str, "%f %f %f rg ", PDF_RGB_R(colour), PDF_RGB_G(colour),
                PDF_RGB_B(colour));
    dstr_printf(&str, "%f Tc ", spacing);
    dstr_append(&str, "(");

    /* Escape magic characters properly */
    for (size_t i = 0; i < len;) {
        int code_len;
        uint8_t pdf_char;
        code_len = utf8_to_pdfencoding(pdf, &text[i], len - i, &pdf_char);
        if (code_len < 0) {
            dstr_free(&str);
            return code_len;
        }

        if (strchr("()\\", pdf_char)) {
            char buf[3];
            /* Escape some characters */
            buf[0] = '\\';
            buf[1] = pdf_char;
            buf[2] = '\0';
            dstr_append(&str, buf);
        } else if (strrchr("\n\r\t\b\f", pdf_char)) {
            /* Skip over these characters */
            ;
        } else {
            dstr_append_data(&str, &pdf_char, 1);
        }

        i += code_len;
    }
    dstr_append(&str, ") Tj ");
    dstr_append(&str, "ET");

    ret = pdf_add_stream(pdf, page, dstr_data(&str));
    dstr_free(&str);
    return ret;
}

int pdf_add_text(pdf_doc *pdf, Object *page,
                 const char *text, float size, float xoff, float yoff,
                 uint32_t colour)
{
    return pdf_add_text_spacing(pdf, page, text, size, xoff, yoff, colour, 0,
                                0);
}

int pdf_add_text_rotate(pdf_doc *pdf, Object *page,
                        const char *text, float size, float xoff, float yoff,
                        float angle, uint32_t colour)
{
    return pdf_add_text_spacing(pdf, page, text, size, xoff, yoff, colour, 0,
                                angle);
}

/* How wide is each character, in points, at size 14 */
static const uint16_t helvetica_widths[256] = {
    280, 280, 280, 280,  280, 280, 280, 280,  280,  280, 280,  280, 280,
    280, 280, 280, 280,  280, 280, 280, 280,  280,  280, 280,  280, 280,
    280, 280, 280, 280,  280, 280, 280, 280,  357,  560, 560,  896, 672,
    192, 335, 335, 392,  588, 280, 335, 280,  280,  560, 560,  560, 560,
    560, 560, 560, 560,  560, 560, 280, 280,  588,  588, 588,  560, 1023,
    672, 672, 727, 727,  672, 615, 784, 727,  280,  504, 672,  560, 839,
    727, 784, 672, 784,  727, 672, 615, 727,  672,  951, 672,  672, 615,
    280, 280, 280, 472,  560, 335, 560, 560,  504,  560, 560,  280, 560,
    560, 223, 223, 504,  223, 839, 560, 560,  560,  560, 335,  504, 280,
    560, 504, 727, 504,  504, 504, 336, 262,  336,  588, 352,  560, 352,
    223, 560, 335, 1008, 560, 560, 335, 1008, 672,  335, 1008, 352, 615,
    352, 352, 223, 223,  335, 335, 352, 560,  1008, 335, 1008, 504, 335,
    951, 352, 504, 672,  280, 335, 560, 560,  560,  560, 262,  560, 335,
    742, 372, 560, 588,  335, 742, 335, 403,  588,  335, 335,  335, 560,
    541, 280, 335, 335,  367, 560, 840, 840,  840,  615, 672,  672, 672,
    672, 672, 672, 1008, 727, 672, 672, 672,  672,  280, 280,  280, 280,
    727, 727, 784, 784,  784, 784, 784, 588,  784,  727, 727,  727, 727,
    672, 672, 615, 560,  560, 560, 560, 560,  560,  896, 504,  560, 560,
    560, 560, 280, 280,  280, 280, 560, 560,  560,  560, 560,  560, 560,
    588, 615, 560, 560,  560, 560, 504, 560,  504,
};

static const uint16_t helvetica_bold_widths[256] = {
    280,  280, 280,  280, 280, 280, 280, 280,  280, 280, 280, 280,  280, 280,
    280,  280, 280,  280, 280, 280, 280, 280,  280, 280, 280, 280,  280, 280,
    280,  280, 280,  280, 280, 335, 477, 560,  560, 896, 727, 239,  335, 335,
    392,  588, 280,  335, 280, 280, 560, 560,  560, 560, 560, 560,  560, 560,
    560,  560, 335,  335, 588, 588, 588, 615,  982, 727, 727, 727,  727, 672,
    615,  784, 727,  280, 560, 727, 615, 839,  727, 784, 672, 784,  727, 672,
    615,  727, 672,  951, 672, 672, 615, 335,  280, 335, 588, 560,  335, 560,
    615,  560, 615,  560, 335, 615, 615, 280,  280, 560, 280, 896,  615, 615,
    615,  615, 392,  560, 335, 615, 560, 784,  560, 560, 504, 392,  282, 392,
    588,  352, 560,  352, 280, 560, 504, 1008, 560, 560, 335, 1008, 672, 335,
    1008, 352, 615,  352, 352, 280, 280, 504,  504, 352, 560, 1008, 335, 1008,
    560,  335, 951,  352, 504, 672, 280, 335,  560, 560, 560, 560,  282, 560,
    335,  742, 372,  560, 588, 335, 742, 335,  403, 588, 335, 335,  335, 615,
    560,  280, 335,  335, 367, 560, 840, 840,  840, 615, 727, 727,  727, 727,
    727,  727, 1008, 727, 672, 672, 672, 672,  280, 280, 280, 280,  727, 727,
    784,  784, 784,  784, 784, 588, 784, 727,  727, 727, 727, 672,  672, 615,
    560,  560, 560,  560, 560, 560, 896, 560,  560, 560, 560, 560,  280, 280,
    280,  280, 615,  615, 615, 615, 615, 615,  615, 588, 615, 615,  615, 615,
    615,  560, 615,  560,
};

static const uint16_t helvetica_bold_oblique_widths[256] = {
    280,  280, 280,  280, 280, 280, 280, 280,  280, 280, 280, 280,  280, 280,
    280,  280, 280,  280, 280, 280, 280, 280,  280, 280, 280, 280,  280, 280,
    280,  280, 280,  280, 280, 335, 477, 560,  560, 896, 727, 239,  335, 335,
    392,  588, 280,  335, 280, 280, 560, 560,  560, 560, 560, 560,  560, 560,
    560,  560, 335,  335, 588, 588, 588, 615,  982, 727, 727, 727,  727, 672,
    615,  784, 727,  280, 560, 727, 615, 839,  727, 784, 672, 784,  727, 672,
    615,  727, 672,  951, 672, 672, 615, 335,  280, 335, 588, 560,  335, 560,
    615,  560, 615,  560, 335, 615, 615, 280,  280, 560, 280, 896,  615, 615,
    615,  615, 392,  560, 335, 615, 560, 784,  560, 560, 504, 392,  282, 392,
    588,  352, 560,  352, 280, 560, 504, 1008, 560, 560, 335, 1008, 672, 335,
    1008, 352, 615,  352, 352, 280, 280, 504,  504, 352, 560, 1008, 335, 1008,
    560,  335, 951,  352, 504, 672, 280, 335,  560, 560, 560, 560,  282, 560,
    335,  742, 372,  560, 588, 335, 742, 335,  403, 588, 335, 335,  335, 615,
    560,  280, 335,  335, 367, 560, 840, 840,  840, 615, 727, 727,  727, 727,
    727,  727, 1008, 727, 672, 672, 672, 672,  280, 280, 280, 280,  727, 727,
    784,  784, 784,  784, 784, 588, 784, 727,  727, 727, 727, 672,  672, 615,
    560,  560, 560,  560, 560, 560, 896, 560,  560, 560, 560, 560,  280, 280,
    280,  280, 615,  615, 615, 615, 615, 615,  615, 588, 615, 615,  615, 615,
    615,  560, 615,  560,
};

static const uint16_t helvetica_oblique_widths[256] = {
    280, 280, 280, 280,  280, 280, 280, 280,  280,  280, 280,  280, 280,
    280, 280, 280, 280,  280, 280, 280, 280,  280,  280, 280,  280, 280,
    280, 280, 280, 280,  280, 280, 280, 280,  357,  560, 560,  896, 672,
    192, 335, 335, 392,  588, 280, 335, 280,  280,  560, 560,  560, 560,
    560, 560, 560, 560,  560, 560, 280, 280,  588,  588, 588,  560, 1023,
    672, 672, 727, 727,  672, 615, 784, 727,  280,  504, 672,  560, 839,
    727, 784, 672, 784,  727, 672, 615, 727,  672,  951, 672,  672, 615,
    280, 280, 280, 472,  560, 335, 560, 560,  504,  560, 560,  280, 560,
    560, 223, 223, 504,  223, 839, 560, 560,  560,  560, 335,  504, 280,
    560, 504, 727, 504,  504, 504, 336, 262,  336,  588, 352,  560, 352,
    223, 560, 335, 1008, 560, 560, 335, 1008, 672,  335, 1008, 352, 615,
    352, 352, 223, 223,  335, 335, 352, 560,  1008, 335, 1008, 504, 335,
    951, 352, 504, 672,  280, 335, 560, 560,  560,  560, 262,  560, 335,
    742, 372, 560, 588,  335, 742, 335, 403,  588,  335, 335,  335, 560,
    541, 280, 335, 335,  367, 560, 840, 840,  840,  615, 672,  672, 672,
    672, 672, 672, 1008, 727, 672, 672, 672,  672,  280, 280,  280, 280,
    727, 727, 784, 784,  784, 784, 784, 588,  784,  727, 727,  727, 727,
    672, 672, 615, 560,  560, 560, 560, 560,  560,  896, 504,  560, 560,
    560, 560, 280, 280,  280, 280, 560, 560,  560,  560, 560,  560, 560,
    588, 615, 560, 560,  560, 560, 504, 560,  504,
};

static const uint16_t symbol_widths[256] = {
    252, 252, 252, 252,  252, 252, 252,  252, 252,  252,  252, 252, 252, 252,
    252, 252, 252, 252,  252, 252, 252,  252, 252,  252,  252, 252, 252, 252,
    252, 252, 252, 252,  252, 335, 718,  504, 553,  839,  784, 442, 335, 335,
    504, 553, 252, 553,  252, 280, 504,  504, 504,  504,  504, 504, 504, 504,
    504, 504, 280, 280,  553, 553, 553,  447, 553,  727,  672, 727, 616, 615,
    769, 607, 727, 335,  636, 727, 691,  896, 727,  727,  774, 746, 560, 596,
    615, 695, 442, 774,  650, 801, 615,  335, 869,  335,  663, 504, 504, 636,
    553, 553, 497, 442,  525, 414, 607,  331, 607,  553,  553, 580, 525, 553,
    553, 525, 553, 607,  442, 580, 718,  691, 496,  691,  497, 483, 201, 483,
    553, 0,   0,   0,    0,   0,   0,    0,   0,    0,    0,   0,   0,   0,
    0,   0,   0,   0,    0,   0,   0,    0,   0,    0,    0,   0,   0,   0,
    0,   0,   0,   0,    0,   0,   756,  624, 248,  553,  168, 718, 504, 759,
    759, 759, 759, 1050, 994, 607, 994,  607, 403,  553,  414, 553, 553, 718,
    497, 463, 553, 553,  553, 553, 1008, 607, 1008, 663,  829, 691, 801, 994,
    774, 774, 829, 774,  774, 718, 718,  718, 718,  718,  718, 718, 774, 718,
    796, 796, 897, 829,  553, 252, 718,  607, 607,  1050, 994, 607, 994, 607,
    497, 331, 796, 796,  792, 718, 387,  387, 387,  387,  387, 387, 497, 497,
    497, 497, 0,   331,  276, 691, 691,  691, 387,  387,  387, 387, 387, 387,
    497, 497, 497, 0,
};

static const uint16_t times_widths[256] = {
    252, 252, 252, 252, 252, 252, 252, 252,  252, 252, 252, 252,  252, 252,
    252, 252, 252, 252, 252, 252, 252, 252,  252, 252, 252, 252,  252, 252,
    252, 252, 252, 252, 252, 335, 411, 504,  504, 839, 784, 181,  335, 335,
    504, 568, 252, 335, 252, 280, 504, 504,  504, 504, 504, 504,  504, 504,
    504, 504, 280, 280, 568, 568, 568, 447,  928, 727, 672, 672,  727, 615,
    560, 727, 727, 335, 392, 727, 615, 896,  727, 727, 560, 727,  672, 560,
    615, 727, 727, 951, 727, 727, 615, 335,  280, 335, 472, 504,  335, 447,
    504, 447, 504, 447, 335, 504, 504, 280,  280, 504, 280, 784,  504, 504,
    504, 504, 335, 392, 280, 504, 504, 727,  504, 504, 447, 483,  201, 483,
    545, 352, 504, 352, 335, 504, 447, 1008, 504, 504, 335, 1008, 560, 335,
    896, 352, 615, 352, 352, 335, 335, 447,  447, 352, 504, 1008, 335, 987,
    392, 335, 727, 352, 447, 727, 252, 335,  504, 504, 504, 504,  201, 504,
    335, 766, 278, 504, 568, 335, 766, 335,  403, 568, 302, 302,  335, 504,
    456, 252, 335, 302, 312, 504, 756, 756,  756, 447, 727, 727,  727, 727,
    727, 727, 896, 672, 615, 615, 615, 615,  335, 335, 335, 335,  727, 727,
    727, 727, 727, 727, 727, 568, 727, 727,  727, 727, 727, 727,  560, 504,
    447, 447, 447, 447, 447, 447, 672, 447,  447, 447, 447, 447,  280, 280,
    280, 280, 504, 504, 504, 504, 504, 504,  504, 568, 504, 504,  504, 504,
    504, 504, 504, 504,
};

static const uint16_t times_bold_widths[256] = {
    252, 252, 252, 252,  252, 252, 252, 252,  252,  252,  252,  252,  252,
    252, 252, 252, 252,  252, 252, 252, 252,  252,  252,  252,  252,  252,
    252, 252, 252, 252,  252, 252, 252, 335,  559,  504,  504,  1008, 839,
    280, 335, 335, 504,  574, 252, 335, 252,  280,  504,  504,  504,  504,
    504, 504, 504, 504,  504, 504, 335, 335,  574,  574,  574,  504,  937,
    727, 672, 727, 727,  672, 615, 784, 784,  392,  504,  784,  672,  951,
    727, 784, 615, 784,  727, 560, 672, 727,  727,  1008, 727,  727,  672,
    335, 280, 335, 585,  504, 335, 504, 560,  447,  560,  447,  335,  504,
    560, 280, 335, 560,  280, 839, 560, 504,  560,  560,  447,  392,  335,
    560, 504, 727, 504,  504, 447, 397, 221,  397,  524,  352,  504,  352,
    335, 504, 504, 1008, 504, 504, 335, 1008, 560,  335,  1008, 352,  672,
    352, 352, 335, 335,  504, 504, 352, 504,  1008, 335,  1008, 392,  335,
    727, 352, 447, 727,  252, 335, 504, 504,  504,  504,  221,  504,  335,
    752, 302, 504, 574,  335, 752, 335, 403,  574,  302,  302,  335,  560,
    544, 252, 335, 302,  332, 504, 756, 756,  756,  504,  727,  727,  727,
    727, 727, 727, 1008, 727, 672, 672, 672,  672,  392,  392,  392,  392,
    727, 727, 784, 784,  784, 784, 784, 574,  784,  727,  727,  727,  727,
    727, 615, 560, 504,  504, 504, 504, 504,  504,  727,  447,  447,  447,
    447, 447, 280, 280,  280, 280, 504, 560,  504,  504,  504,  504,  504,
    574, 504, 560, 560,  560, 560, 504, 560,  504,
};

static const uint16_t times_bold_italic_widths[256] = {
    252, 252, 252, 252, 252, 252, 252, 252,  252, 252, 252, 252,  252, 252,
    252, 252, 252, 252, 252, 252, 252, 252,  252, 252, 252, 252,  252, 252,
    252, 252, 252, 252, 252, 392, 559, 504,  504, 839, 784, 280,  335, 335,
    504, 574, 252, 335, 252, 280, 504, 504,  504, 504, 504, 504,  504, 504,
    504, 504, 335, 335, 574, 574, 574, 504,  838, 672, 672, 672,  727, 672,
    672, 727, 784, 392, 504, 672, 615, 896,  727, 727, 615, 727,  672, 560,
    615, 727, 672, 896, 672, 615, 615, 335,  280, 335, 574, 504,  335, 504,
    504, 447, 504, 447, 335, 504, 560, 280,  280, 504, 280, 784,  560, 504,
    504, 504, 392, 392, 280, 560, 447, 672,  504, 447, 392, 350,  221, 350,
    574, 352, 504, 352, 335, 504, 504, 1008, 504, 504, 335, 1008, 560, 335,
    951, 352, 615, 352, 352, 335, 335, 504,  504, 352, 504, 1008, 335, 1008,
    392, 335, 727, 352, 392, 615, 252, 392,  504, 504, 504, 504,  221, 504,
    335, 752, 268, 504, 610, 335, 752, 335,  403, 574, 302, 302,  335, 580,
    504, 252, 335, 302, 302, 504, 756, 756,  756, 504, 672, 672,  672, 672,
    672, 672, 951, 672, 672, 672, 672, 672,  392, 392, 392, 392,  727, 727,
    727, 727, 727, 727, 727, 574, 727, 727,  727, 727, 727, 615,  615, 504,
    504, 504, 504, 504, 504, 504, 727, 447,  447, 447, 447, 447,  280, 280,
    280, 280, 504, 560, 504, 504, 504, 504,  504, 574, 504, 560,  560, 560,
    560, 447, 504, 447,
};

static const uint16_t times_italic_widths[256] = {
    252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252,  252, 252,
    252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252,  252, 252,
    252, 252, 252, 252, 252, 335, 423, 504, 504, 839, 784, 215,  335, 335,
    504, 680, 252, 335, 252, 280, 504, 504, 504, 504, 504, 504,  504, 504,
    504, 504, 335, 335, 680, 680, 680, 504, 927, 615, 615, 672,  727, 615,
    615, 727, 727, 335, 447, 672, 560, 839, 672, 727, 615, 727,  615, 504,
    560, 727, 615, 839, 615, 560, 560, 392, 280, 392, 425, 504,  335, 504,
    504, 447, 504, 447, 280, 504, 504, 280, 280, 447, 280, 727,  504, 504,
    504, 504, 392, 392, 280, 504, 447, 672, 447, 447, 392, 403,  277, 403,
    545, 352, 504, 352, 335, 504, 560, 896, 504, 504, 335, 1008, 504, 335,
    951, 352, 560, 352, 352, 335, 335, 560, 560, 352, 504, 896,  335, 987,
    392, 335, 672, 352, 392, 560, 252, 392, 504, 504, 504, 504,  277, 504,
    335, 766, 278, 504, 680, 335, 766, 335, 403, 680, 302, 302,  335, 504,
    527, 252, 335, 302, 312, 504, 756, 756, 756, 504, 615, 615,  615, 615,
    615, 615, 896, 672, 615, 615, 615, 615, 335, 335, 335, 335,  727, 672,
    727, 727, 727, 727, 727, 680, 727, 727, 727, 727, 727, 560,  615, 504,
    504, 504, 504, 504, 504, 504, 672, 447, 447, 447, 447, 447,  280, 280,
    280, 280, 504, 504, 504, 504, 504, 504, 504, 680, 504, 504,  504, 504,
    504, 447, 504, 447,
};

static const uint16_t zapfdingbats_widths[256] = {
    0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   280,  981, 968, 981, 987, 724, 795, 796, 797, 695,
    967, 946, 553, 861, 918,  940, 918, 952, 981, 761, 852, 768, 767, 575,
    682, 769, 766, 765, 760,  497, 556, 541, 581, 697, 792, 794, 794, 796,
    799, 800, 822, 829, 795,  847, 829, 839, 822, 837, 930, 749, 728, 754,
    796, 798, 700, 782, 774,  798, 765, 712, 713, 687, 706, 832, 821, 795,
    795, 712, 692, 701, 694,  792, 793, 718, 797, 791, 797, 879, 767, 768,
    768, 765, 765, 899, 899,  794, 790, 441, 139, 279, 418, 395, 395, 673,
    673, 0,   393, 393, 319,  319, 278, 278, 513, 513, 413, 413, 235, 235,
    336, 336, 0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,    0,   0,   737, 548, 548, 917, 672, 766, 766,
    782, 599, 699, 631, 794,  794, 794, 794, 794, 794, 794, 794, 794, 794,
    794, 794, 794, 794, 794,  794, 794, 794, 794, 794, 794, 794, 794, 794,
    794, 794, 794, 794, 794,  794, 794, 794, 794, 794, 794, 794, 794, 794,
    794, 794, 901, 844, 1024, 461, 753, 931, 753, 925, 934, 935, 935, 840,
    879, 834, 931, 931, 924,  937, 938, 466, 890, 842, 842, 873, 873, 701,
    701, 880, 0,   880, 766,  953, 777, 871, 777, 895, 974, 895, 837, 879,
    934, 977, 925, 0,
};

static const uint16_t courier_widths[256] = {
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604,
};

static int pdf_text_point_width(pdf_doc *pdf, const char *text,
                                ptrdiff_t text_len, float size,
                                const uint16_t *widths, float *point_width)
{
    uint32_t len = 0;
    if (text_len < 0)
        text_len = strlen(text);
    *point_width = 0.0f;

    for (int i = 0; i < (int)text_len;) {
        uint8_t pdf_char = 0;
        int code_len;
        code_len =
            utf8_to_pdfencoding(pdf, &text[i], text_len - i, &pdf_char);
        if (code_len < 0)
            return SetErr(code_len,
                               "Invalid unicode string at position %d in %s",
                               i, text);
        i += code_len;

        if (pdf_char != '\n' && pdf_char != '\r')
            len += widths[pdf_char];
    }

    /* Our widths arrays are for 14pt fonts */
    *point_width = len * size / (14.0f * 72.0f);

    return 0;
}

static const uint16_t *find_font_widths(const char *font_name)
{
    if (strcasecmp(font_name, "Helvetica") == 0)
        return helvetica_widths;
    if (strcasecmp(font_name, "Helvetica-Bold") == 0)
        return helvetica_bold_widths;
    if (strcasecmp(font_name, "Helvetica-BoldOblique") == 0)
        return helvetica_bold_oblique_widths;
    if (strcasecmp(font_name, "Helvetica-Oblique") == 0)
        return helvetica_oblique_widths;
    if (strcasecmp(font_name, "Courier") == 0 ||
        strcasecmp(font_name, "Courier-Bold") == 0 ||
        strcasecmp(font_name, "Courier-BoldOblique") == 0 ||
        strcasecmp(font_name, "Courier-Oblique") == 0)
        return courier_widths;
    if (strcasecmp(font_name, "Times-Roman") == 0)
        return times_widths;
    if (strcasecmp(font_name, "Times-Bold") == 0)
        return times_bold_widths;
    if (strcasecmp(font_name, "Times-Italic") == 0)
        return times_italic_widths;
    if (strcasecmp(font_name, "Times-BoldItalic") == 0)
        return times_bold_italic_widths;
    if (strcasecmp(font_name, "Symbol") == 0)
        return symbol_widths;
    if (strcasecmp(font_name, "ZapfDingbats") == 0)
        return zapfdingbats_widths;

    return nullptr;
}

int pdf_get_font_text_width(pdf_doc *pdf, const char *font_name,
                            const char *text, float size, float *text_width)
{
    if (!font_name)
        font_name = pdf->current_font->font.name;
    const uint16_t *widths = find_font_widths(font_name);

    if (!widths)
        return SetErr(-EINVAL,
                           "Unable to determine width for font '%s'",
                           pdf->current_font->font.name);
    return pdf_text_point_width(pdf, text, -1, size, widths, text_width);
}

static const char *find_word_break(const char *string)
{
    if (!string)
        return nullptr;
    /* Skip over the actual word */
    while (*string && !isspace(*string))
        string++;

    return string;
}

int pdf_add_text_wrap(pdf_doc *pdf, Object *page,
                      const char *text, float size, float xoff, float yoff,
                      float angle, uint32_t colour, float wrap_width,
                      int align, float *height)
{
    /* Move through the text string, stopping at word boundaries,
     * trying to find the longest text string we can fit in the given width
     */
    const char *start = text;
    const char *last_best = text;
    const char *end = text;
    char line[512];
    const uint16_t *widths;
    float orig_yoff = yoff;

    widths = find_font_widths(pdf->current_font->font.name);
    if (!widths)
        return SetErr(-EINVAL,
                           "Unable to determine width for font '%s'",
                           pdf->current_font->font.name);

    while (start && *start) {
        const char *new_end = find_word_break(end + 1);
        float line_width;
        int output = 0;
        float xoff_align = xoff;
        int e;

        end = new_end;

        e = pdf_text_point_width(pdf, start, end - start, size, widths,
                                 &line_width);
        if (e < 0)
            return e;

        if (line_width >= wrap_width) {
            if (last_best == start) {
                /* There is a single word that is too long for the line */
                ptrdiff_t i;
                /* Find the best character to chop it at */
                for (i = end - start - 1; i > 0; i--) {
                    float this_width;
                    // Don't look at places that are in the middle of a utf-8
                    // sequence
                    if ((start[i - 1] & 0xc0) == 0xc0 ||
                        ((start[i - 1] & 0xc0) == 0x80 &&
                         (start[i] & 0xc0) == 0x80))
                        continue;
                    e = pdf_text_point_width(pdf, start, i, size, widths,
                                             &this_width);
                    if (e < 0)
                        return e;
                    if (this_width < wrap_width)
                        break;
                }
                if (i == 0)
                    return SetErr(-EINVAL,
                                       "Unable to find suitable line break");

                end = start + i;
            } else
                end = last_best;
            output = 1;
        }
        if (*end == '\0')
            output = 1;

        if (*end == '\n' || *end == '\r')
            output = 1;

        if (output) {
            int len = end - start;
            float char_spacing = 0;
            if (len >= (int)sizeof(line))
                len = (int)sizeof(line) - 1;
            strncpy(line, start, len);
            line[len] = '\0';

            e = pdf_text_point_width(pdf, start, len, size, widths,
                                     &line_width);
            if (e < 0)
                return e;

            switch (align) {
            case PDF_ALIGN_RIGHT:
                xoff_align += wrap_width - line_width;
                break;
            case PDF_ALIGN_CENTER:
                xoff_align += (wrap_width - line_width) / 2;
                break;
            case PDF_ALIGN_JUSTIFY:
                if ((len - 1) > 0 && *end != '\r' && *end != '\n' &&
                    *end != '\0')
                    char_spacing = (wrap_width - line_width) / (len - 2);
                break;
            case PDF_ALIGN_JUSTIFY_ALL:
                if ((len - 1) > 0)
                    char_spacing = (wrap_width - line_width) / (len - 2);
                break;
            }

            if (align != PDF_ALIGN_NO_WRITE) {
                pdf_add_text_spacing(pdf, page, line, size, xoff_align, yoff,
                                     colour, char_spacing, angle);
            }

            if (*end == ' ')
                end++;

            start = last_best = end;
            yoff -= size;
        } else
            last_best = end;
    }

    if (height)
        *height = orig_yoff - yoff;
    return 0;
}

int pdf_add_line(pdf_doc *pdf, Object *page, float x1,
                 float y1, float x2, float y2, float width, uint32_t colour)
{
    int ret;
    dstr str = INIT_DSTR;

    dstr_printf(&str, "%f w\r\n", width);
    dstr_printf(&str, "%f %f m\r\n", x1, y1);
    dstr_printf(&str, "/DeviceRGB CS\r\n");
    dstr_printf(&str, "%f %f %f RG\r\n", PDF_RGB_R(colour), PDF_RGB_G(colour),
                PDF_RGB_B(colour));
    dstr_printf(&str, "%f %f l S\r\n", x2, y2);

    ret = pdf_add_stream(pdf, page, dstr_data(&str));
    dstr_free(&str);

    return ret;
}

int pdf_add_cubic_bezier(pdf_doc *pdf, Object *page,
                         float x1, float y1, float x2, float y2, float xq1,
                         float yq1, float xq2, float yq2, float width,
                         uint32_t colour)
{
    int ret;
    dstr str = INIT_DSTR;

    dstr_printf(&str, "%f w\r\n", width);
    dstr_printf(&str, "%f %f m\r\n", x1, y1);
    dstr_printf(&str, "/DeviceRGB CS\r\n");
    dstr_printf(&str, "%f %f %f RG\r\n", PDF_RGB_R(colour), PDF_RGB_G(colour),
                PDF_RGB_B(colour));
    dstr_printf(&str, "%f %f %f %f %f %f c S\r\n", xq1, yq1, xq2, yq2, x2,
                y2);

    ret = pdf_add_stream(pdf, page, dstr_data(&str));
    dstr_free(&str);

    return ret;
}

int pdf_add_quadratic_bezier(pdf_doc *pdf, Object *page,
                             float x1, float y1, float x2, float y2,
                             float xq1, float yq1, float width,
                             uint32_t colour)
{
    float xc1 = x1 + (xq1 - x1) * (2.0f / 3.0f);
    float yc1 = y1 + (yq1 - y1) * (2.0f / 3.0f);
    float xc2 = x2 + (xq1 - x2) * (2.0f / 3.0f);
    float yc2 = y2 + (yq1 - y2) * (2.0f / 3.0f);
    return pdf_add_cubic_bezier(pdf, page, x1, y1, x2, y2, xc1, yc1, xc2, yc2,
                                width, colour);
}

int pdf_add_custom_path(pdf_doc *pdf, Object *page,
                        const struct pdf_path_operation *operations,
                        int operation_count, float stroke_width,
                        uint32_t stroke_colour, uint32_t fill_colour)
{
    int ret;
    dstr str = INIT_DSTR;

    if (!PDF_IS_TRANSPARENT(fill_colour)) {
        dstr_printf(&str, "/DeviceRGB CS\r\n");
        dstr_printf(&str, "%f %f %f rg\r\n", PDF_RGB_R(fill_colour),
                    PDF_RGB_G(fill_colour), PDF_RGB_B(fill_colour));
    }
    dstr_printf(&str, "%f w\r\n", stroke_width);
    dstr_printf(&str, "/DeviceRGB CS\r\n");
    dstr_printf(&str, "%f %f %f RG\r\n", PDF_RGB_R(stroke_colour),
                PDF_RGB_G(stroke_colour), PDF_RGB_B(stroke_colour));

    for (int i = 0; i < operation_count; i++) {
        struct pdf_path_operation operation = operations[i];
        switch (operation.op) {
        case 'm':
            dstr_printf(&str, "%f %f m\r\n", operation.x1, operation.y1);
            break;
        case 'l':
            dstr_printf(&str, "%f %f l\r\n", operation.x1, operation.y1);
            break;
        case 'c':
            dstr_printf(&str, "%f %f %f %f %f %f c\r\n", operation.x1,
                        operation.y1, operation.x2, operation.y2,
                        operation.x3, operation.y3);
            break;
        case 'v':
            dstr_printf(&str, "%f %f %f %f v\r\n", operation.x1, operation.y1,
                        operation.x2, operation.y2);
            break;
        case 'y':
            dstr_printf(&str, "%f %f %f %f y\r\n", operation.x1, operation.y1,
                        operation.x2, operation.y2);
            break;
        case 'h':
            dstr_printf(&str, "h\r\n");
            break;
        default:
            return SetErr(-errno, "Invalid operation");
            break;
        }
    }

    if (PDF_IS_TRANSPARENT(fill_colour))
        dstr_printf(&str, "%s", "S ");
    else
        dstr_printf(&str, "%s", "B ");
    ret = pdf_add_stream(pdf, page, dstr_data(&str));
    dstr_free(&str);

    return ret;
}

int pdf_add_ellipse(pdf_doc *pdf, Object *page, float x,
                    float y, float xradius, float yradius, float width,
                    uint32_t colour, uint32_t fill_colour)
{
    int ret;
    dstr str = INIT_DSTR;
    float lx, ly;

    lx = (4.0f / 3.0f) * (float)(M_SQRT2 - 1) * xradius;
    ly = (4.0f / 3.0f) * (float)(M_SQRT2 - 1) * yradius;

    if (!PDF_IS_TRANSPARENT(fill_colour)) {
        dstr_printf(&str, "/DeviceRGB CS\r\n");
        dstr_printf(&str, "%f %f %f rg\r\n", PDF_RGB_R(fill_colour),
                    PDF_RGB_G(fill_colour), PDF_RGB_B(fill_colour));
    }

    /* stroke color */
    dstr_printf(&str, "/DeviceRGB CS\r\n");
    dstr_printf(&str, "%f %f %f RG\r\n", PDF_RGB_R(colour), PDF_RGB_G(colour),
                PDF_RGB_B(colour));

    dstr_printf(&str, "%f w ", width);

    dstr_printf(&str, "%.2f %.2f m ", (x + xradius), (y));

    dstr_printf(&str, "%.2f %.2f %.2f %.2f %.2f %.2f c ", (x + xradius),
                (y - ly), (x + lx), (y - yradius), x, (y - yradius));

    dstr_printf(&str, "%.2f %.2f %.2f %.2f %.2f %.2f c ", (x - lx),
                (y - yradius), (x - xradius), (y - ly), (x - xradius), y);

    dstr_printf(&str, "%.2f %.2f %.2f %.2f %.2f %.2f c ", (x - xradius),
                (y + ly), (x - lx), (y + yradius), x, (y + yradius));

    dstr_printf(&str, "%.2f %.2f %.2f %.2f %.2f %.2f c ", (x + lx),
                (y + yradius), (x + xradius), (y + ly), (x + xradius), y);

    if (PDF_IS_TRANSPARENT(fill_colour))
        dstr_printf(&str, "%s", "S ");
    else
        dstr_printf(&str, "%s", "B ");

    ret = pdf_add_stream(pdf, page, dstr_data(&str));
    dstr_free(&str);

    return ret;
}

int pdf_add_circle(pdf_doc *pdf, Object *page, float xr,
                   float yr, float radius, float width, uint32_t colour,
                   uint32_t fill_colour)
{
    return pdf_add_ellipse(pdf, page, xr, yr, radius, radius, width, colour,
                           fill_colour);
}

int pdf_add_rectangle(pdf_doc *pdf, Object *page, float x,
                      float y, float width, float height, float border_width,
                      uint32_t colour)
{
    int ret;
    dstr str = INIT_DSTR;

    dstr_printf(&str, "%f %f %f RG ", PDF_RGB_R(colour), PDF_RGB_G(colour),
                PDF_RGB_B(colour));
    dstr_printf(&str, "%f w ", border_width);
    dstr_printf(&str, "%f %f %f %f re S ", x, y, width, height);

    ret = pdf_add_stream(pdf, page, dstr_data(&str));
    dstr_free(&str);

    return ret;
}

int pdf_add_filled_rectangle(pdf_doc *pdf, Object *page,
                             float x, float y, float width, float height,
                             float border_width, uint32_t colour_fill,
                             uint32_t colour_border)
{
    int ret;
    dstr str = INIT_DSTR;

    dstr_printf(&str, "%f %f %f rg ", PDF_RGB_R(colour_fill),
                PDF_RGB_G(colour_fill), PDF_RGB_B(colour_fill));
    if (border_width > 0) {
        dstr_printf(&str, "%f %f %f RG ", PDF_RGB_R(colour_border),
                    PDF_RGB_G(colour_border), PDF_RGB_B(colour_border));
        dstr_printf(&str, "%f w ", border_width);
        dstr_printf(&str, "%f %f %f %f re B ", x, y, width, height);
    } else {
        dstr_printf(&str, "%f %f %f %f re f ", x, y, width, height);
    }

    ret = pdf_add_stream(pdf, page, dstr_data(&str));
    dstr_free(&str);

    return ret;
}

int pdf_add_polygon(pdf_doc *pdf, Object *page, float x[],
                    float y[], int count, float border_width, uint32_t colour)
{
    int ret;
    dstr str = INIT_DSTR;

    dstr_printf(&str, "%f %f %f RG ", PDF_RGB_R(colour), PDF_RGB_G(colour),
                PDF_RGB_B(colour));
    dstr_printf(&str, "%f w ", border_width);
    dstr_printf(&str, "%f %f m ", x[0], y[0]);
    for (int i = 1; i < count; i++) {
        dstr_printf(&str, "%f %f l ", x[i], y[i]);
    }
    dstr_printf(&str, "h S ");

    ret = pdf_add_stream(pdf, page, dstr_data(&str));
    dstr_free(&str);

    return ret;
}

int pdf_add_filled_polygon(pdf_doc *pdf, Object *page,
                           float x[], float y[], int count,
                           float border_width, uint32_t colour)
{
    int ret;
    dstr str = INIT_DSTR;

    dstr_printf(&str, "%f %f %f RG ", PDF_RGB_R(colour), PDF_RGB_G(colour),
                PDF_RGB_B(colour));
    dstr_printf(&str, "%f %f %f rg ", PDF_RGB_R(colour), PDF_RGB_G(colour),
                PDF_RGB_B(colour));
    dstr_printf(&str, "%f w ", border_width);
    dstr_printf(&str, "%f %f m ", x[0], y[0]);
    for (int i = 1; i < count; i++) {
        dstr_printf(&str, "%f %f l ", x[i], y[i]);
    }
    dstr_printf(&str, "h f ");

    ret = pdf_add_stream(pdf, page, dstr_data(&str));
    dstr_free(&str);

    return ret;
}

static const struct {
    uint32_t code;
    char ch;
} code_128a_encoding[] = {
    {0x212222, ' '},  {0x222122, '!'},  {0x222221, '"'},   {0x121223, '#'},
    {0x121322, '$'},  {0x131222, '%'},  {0x122213, '&'},   {0x122312, '\''},
    {0x132212, '('},  {0x221213, ')'},  {0x221312, '*'},   {0x231212, '+'},
    {0x112232, ','},  {0x122132, '-'},  {0x122231, '.'},   {0x113222, '/'},
    {0x123122, '0'},  {0x123221, '1'},  {0x223211, '2'},   {0x221132, '3'},
    {0x221231, '4'},  {0x213212, '5'},  {0x223112, '6'},   {0x312131, '7'},
    {0x311222, '8'},  {0x321122, '9'},  {0x321221, ':'},   {0x312212, ';'},
    {0x322112, '<'},  {0x322211, '='},  {0x212123, '>'},   {0x212321, '?'},
    {0x232121, '@'},  {0x111323, 'A'},  {0x131123, 'B'},   {0x131321, 'C'},
    {0x112313, 'D'},  {0x132113, 'E'},  {0x132311, 'F'},   {0x211313, 'G'},
    {0x231113, 'H'},  {0x231311, 'I'},  {0x112133, 'J'},   {0x112331, 'K'},
    {0x132131, 'L'},  {0x113123, 'M'},  {0x113321, 'N'},   {0x133121, 'O'},
    {0x313121, 'P'},  {0x211331, 'Q'},  {0x231131, 'R'},   {0x213113, 'S'},
    {0x213311, 'T'},  {0x213131, 'U'},  {0x311123, 'V'},   {0x311321, 'W'},
    {0x331121, 'X'},  {0x312113, 'Y'},  {0x312311, 'Z'},   {0x332111, '['},
    {0x314111, '\\'}, {0x221411, ']'},  {0x431111, '^'},   {0x111224, '_'},
    {0x111422, '`'},  {0x121124, 'a'},  {0x121421, 'b'},   {0x141122, 'c'},
    {0x141221, 'd'},  {0x112214, 'e'},  {0x112412, 'f'},   {0x122114, 'g'},
    {0x122411, 'h'},  {0x142112, 'i'},  {0x142211, 'j'},   {0x241211, 'k'},
    {0x221114, 'l'},  {0x413111, 'm'},  {0x241112, 'n'},   {0x134111, 'o'},
    {0x111242, 'p'},  {0x121142, 'q'},  {0x121241, 'r'},   {0x114212, 's'},
    {0x124112, 't'},  {0x124211, 'u'},  {0x411212, 'v'},   {0x421112, 'w'},
    {0x421211, 'x'},  {0x212141, 'y'},  {0x214121, 'z'},   {0x412121, '{'},
    {0x111143, '|'},  {0x111341, '}'},  {0x131141, '~'},   {0x114113, '\0'},
    {0x114311, '\0'}, {0x411113, '\0'}, {0x411311, '\0'},  {0x113141, '\0'},
    {0x114131, '\0'}, {0x311141, '\0'}, {0x411131, '\0'},  {0x211412, '\0'},
    {0x211214, '\0'}, {0x211232, '\0'}, {0x2331112, '\0'},
};

static int find_128_encoding(char ch)
{
    for (int i = 0; i < (int)ARRAY_SIZE(code_128a_encoding); i++) {
        if (code_128a_encoding[i].ch == ch)
            return i;
    }
    return -1;
}

static float pdf_barcode_128a_ch(pdf_doc *pdf, Object *page,
                                 float x, float y, float width, float height,
                                 uint32_t colour, int index, int code_len)
{
    uint32_t code = code_128a_encoding[index].code;
    float line_width = width / 11.0f;

    for (int i = 0; i < code_len; i++) {
        uint8_t shift = (code_len - 1 - i) * 4;
        uint8_t mask = (code >> shift) & 0xf;

        if (!(i % 2))
            pdf_add_filled_rectangle(pdf, page, x, y, line_width * mask,
                                     height, 0, colour, PDF_TRANSPARENT);
        x += line_width * mask;
    }
    return x;
}

static int pdf_add_barcode_128a(pdf_doc *pdf, Object *page,
                                float x, float y, float width, float height,
                                const char *string, uint32_t colour)
{
    const char *s;
    size_t len = strlen(string) + 3;
    float char_width = width / len;
    int checksum, i;

    if (char_width / 11.0f <= 0)
        return SetErr(-EINVAL,
                           "Insufficient width to draw barcode");

    for (s = string; *s; s++)
        if (find_128_encoding(*s) < 0)
            return SetErr(-EINVAL, "Invalid barcode character 0x%x",
                               *s);

    x = pdf_barcode_128a_ch(pdf, page, x, y, char_width, height, colour, 104,
                            6);
    checksum = 104;

    for (i = 1, s = string; *s; s++, i++) {
        int index = find_128_encoding(*s);
        // This should be impossible, due to the checks above, but confirm
        // here anyway to stop coverity complaining
        if (index < 0)
            return SetErr(-EINVAL,
                               "Invalid 128a barcode character 0x%x", *s);
        x = pdf_barcode_128a_ch(pdf, page, x, y, char_width, height, colour,
                                index, 6);
        checksum += index * i;
    }
    x = pdf_barcode_128a_ch(pdf, page, x, y, char_width, height, colour,
                            checksum % 103, 6);
    pdf_barcode_128a_ch(pdf, page, x, y, char_width, height, colour, 106, 7);
    return 0;
}

/* Code 39 character encoding. Each 4-bit value indicates:
 * 0 => wide bar
 * 1 => narrow bar
 * 2 => wide space
 */
static const struct {
    int code;
    char ch;
} code_39_encoding[] = {
    {0x012110, '1'}, {0x102110, '2'}, {0x002111, '3'},
    {0x112010, '4'}, {0x012011, '5'}, {0x102011, '6'},
    {0x112100, '7'}, {0x012101, '8'}, {0x102101, '9'},
    {0x112001, '0'}, {0x011210, 'A'}, {0x101210, 'B'},
    {0x001211, 'C'}, {0x110210, 'D'}, {0x010211, 'E'},
    {0x100211, 'F'}, {0x111200, 'G'}, {0x011201, 'H'},
    {0x101201, 'I'}, {0x110201, 'J'}, {0x011120, 'K'},
    {0x101120, 'L'}, {0x001121, 'M'}, {0x110120, 'N'},
    {0x010121, 'O'}, {0x100121, 'P'}, {0x111020, 'Q'},
    {0x011021, 'R'}, {0x101021, 'S'}, {0x110021, 'T'},
    {0x021110, 'U'}, {0x120110, 'V'}, {0x020111, 'W'},
    {0x121010, 'X'}, {0x021011, 'Y'}, {0x120011, 'Z'},
    {0x121100, '-'}, {0x021101, '.'}, {0x120101, ' '},
    {0x121001, '*'}, // 'stop' character
};

static int find_39_encoding(char ch)
{
    for (int i = 0; i < (int)ARRAY_SIZE(code_39_encoding); i++) {
        if (code_39_encoding[i].ch == ch) {
            return code_39_encoding[i].code;
        }
    }
    return -1;
}

static int pdf_barcode_39_ch(pdf_doc *pdf, Object *page,
                             float x, float y, float char_width, float height,
                             uint32_t colour, char ch, float *new_x)
{
    float nw = char_width / 12.0f;
    float ww = char_width / 4.0f;
    int code = 0;

    code = find_39_encoding(ch);
    if (code < 0)
        return SetErr(-EINVAL, "Invalid Code 39 character %c 0x%x",
                           ch, ch);

    for (int i = 5; i >= 0; i--) {
        int pattern = (code >> i * 4) & 0xf;
        if (pattern == 0) { // wide
            if (pdf_add_filled_rectangle(pdf, page, x, y, ww - 1, height, 0,
                                         colour, PDF_TRANSPARENT) < 0)
                return pdf->errval;
            x += ww;
        }
        if (pattern == 1) { // narrow
            if (pdf_add_filled_rectangle(pdf, page, x, y, nw - 1, height, 0,
                                         colour, PDF_TRANSPARENT) < 0)
                return pdf->errval;
            x += nw;
        }
        if (pattern == 2) { // space
            x += nw;
        }
    }
    if (new_x)
        *new_x = x;
    return 0;
}

static int pdf_add_barcode_39(pdf_doc *pdf, Object *page,
                              float x, float y, float width, float height,
                              const char *string, uint32_t colour)
{
    size_t len = strlen(string);
    float char_width = width / (len + 2);
    int e;

    e = pdf_barcode_39_ch(pdf, page, x, y, char_width, height, colour, '*',
                          &x);
    if (e < 0)
        return e;

    while (string && *string) {
        e = pdf_barcode_39_ch(pdf, page, x, y, char_width, height, colour,
                              *string, &x);
        if (e < 0)
            return e;
        string++;
    }

    e = pdf_barcode_39_ch(pdf, page, x, y, char_width, height, colour, '*',
                          nullptr);
    if (e < 0)
        return e;

    return 0;
}

/* EAN/UPC character encoding. Each 4-bit value indicates width in x units.
 * Elements are SBSB (Space, Bar, Space, Bar) for LHS digits
 * Elements are inverted for RHS digits
 */
static const int code_eanupc_encoding[] = {
    0x3211, // 0
    0x2221, // 1
    0x2122, // 2
    0x1411, // 3
    0x1132, // 4
    0x1231, // 5
    0x1114, // 6
    0x1312, // 7
    0x1213, // 8
    0x3112, // 9
};

/**
 * List of different barcode guard patterns that are supported
 */
enum {
    GUARD_NORMAL,  //!< Produce normal guard pattern
    GUARD_CENTRE,  //!< Produce centre guard pattern
    GUARD_SPECIAL, //!< Produce special guard pattern
    GUARD_ADDON,
    GUARD_ADDON_DELIN,
};

static const int code_eanupc_aux_encoding[] = {
    0x150, // Normal guard
    0x554, // Centre guard
    0x555, // Special guard
    0x160, // Add-on guard
    0x500, // Add-on delineator
};

static const int set_ean13_encoding[] = {
    0x00, // 0
    0x34, // 1
    0x2c, // 2
    0x1c, // 3
    0x32, // 4
    0x26, // 5
    0x0e, // 6
    0x2a, // 7
    0x1a, // 8
    0x16, // 9
};

static const int set_upce_encoding[] = {
    0x07, // 0
    0x0b, // 1
    0x13, // 2
    0x23, // 3
    0x0d, // 4
    0x19, // 5
    0x31, // 6
    0x15, // 7
    0x25, // 8
    0x29, // 9
};

#define EANUPC_X PDF_MM_TO_POINT(0.330f)

static const struct {
    unsigned modules;
    float height_bar;
    float height_outer;
    unsigned quiet_left;
    unsigned quiet_right;
} eanupc_dimensions[] = {
    {113, PDF_MM_TO_POINT(22.85), PDF_MM_TO_POINT(25.93), 11, 7}, // EAN-13
    {113, PDF_MM_TO_POINT(22.85), PDF_MM_TO_POINT(25.93), 9, 9},  // UPC-A
    {81, PDF_MM_TO_POINT(18.23), PDF_MM_TO_POINT(21.31), 7, 7},   // EAN-8
    {67, PDF_MM_TO_POINT(22.85), PDF_MM_TO_POINT(25.93), 9, 7},   // UPC-E
};

static void pdf_barcode_eanupc_calc_dims(int type, float width, float height,
                                         float *x_off, float *y_off,
                                         float *new_width, float *new_height,
                                         float *x, float *bar_height,
                                         float *bar_ext, float *font_size)
{
    float aspectBarcode, aspectRect, scale;

    aspectRect = width / height;
    aspectBarcode = eanupc_dimensions[type - PDF_BARCODE_EAN13].modules *
                    EANUPC_X /
                    eanupc_dimensions[type - PDF_BARCODE_EAN13].height_outer;
    if (aspectRect > aspectBarcode) {
        *new_height = height;
        *new_width = height * aspectBarcode;
    } else if (aspectRect < aspectBarcode) {
        *new_width = width;
        *new_height = width / aspectBarcode;
    } else {
        *new_width = width;
        *new_height = height;
    }
    scale = *new_height /
            eanupc_dimensions[type - PDF_BARCODE_EAN13].height_outer;

    *x = *new_width / eanupc_dimensions[type - PDF_BARCODE_EAN13].modules;
    *bar_ext = *x * 5;
    *bar_height =
        eanupc_dimensions[type - PDF_BARCODE_EAN13].height_bar * scale;
    *font_size = 8.0f * scale;
    *x_off = (width - *new_width) / 2.0f;
    *y_off = (height - *new_height) / 2.0f;
}

static int pdf_barcode_eanupc_ch(pdf_doc *pdf, Object *page,
                                 float x, float y, float x_width,
                                 float height, uint32_t colour, char ch,
                                 int set, float *new_x)
{
    if ('0' > ch || ch > '9')
        return SetErr(-EINVAL, "Invalid EAN/UPC character %c 0x%x",
                           ch, ch);

    int code;
    code = code_eanupc_encoding[ch - '0'];

    for (int i = 3; i >= 0; i--) {
        int shift = (set == 1 ? 3 - i : i) * 4;
        int bar = (set == 2 && i & 0x1) || (set != 2 && (i & 0x1) == 0);
        float width = (float)((code >> shift) & 0xf);

        switch (ch) {
        case '1':
        case '2':
            if ((set == 0 && bar) || (set != 0 && !bar)) {
                width -= 1.0f / 13.0f;
            } else {
                width += 1.0f / 13.0f;
            }
            break;

        case '7':
        case '8':
            if ((set == 0 && bar) || (set != 0 && !bar)) {
                width += 1.0f / 13.0f;
            } else {
                width -= 1.0f / 13.0f;
            }
            break;
        }

        width *= x_width;
        if (bar) {
            if (pdf_add_filled_rectangle(pdf, page, x, y, width, height, 0,
                                         colour, PDF_TRANSPARENT) < 0)
                return pdf->errval;
        }
        x += width;
    }
    if (new_x)
        *new_x = x;
    return 0;
}

static int pdf_barcode_eanupc_aux(pdf_doc *pdf,
                                  Object *page, float x, float y,
                                  float x_width, float height,
                                  uint32_t colour, int guard_type,
                                  float *new_x)
{
    int code = code_eanupc_aux_encoding[guard_type];

    for (int i = 5; i >= 0; i--) {
        int value = code >> i * 2 & 0x3;
        if (value) {
            if ((i & 0x1) == 0) {
                if (pdf_add_filled_rectangle(pdf, page, x, y, x_width * value,
                                             height, 0, colour,
                                             PDF_TRANSPARENT) < 0)
                    return pdf->errval;
            }
            x += x_width * value;
        }
    }
    if (new_x)
        *new_x = x;
    return 0;
}

static int pdf_add_barcode_ean13(pdf_doc *pdf, Object *page,
                                 float x, float y, float width, float height,
                                 const char *string, uint32_t colour)
{
    if (!string)
        return 0;

    size_t len = strlen(string);
    int lead = 0;
    if (len == 13) {
        char ch = string[0];
        if (!isdigit(*string))
            return SetErr(-EINVAL,
                               "Invalid EAN13 character %c 0x%x", ch, ch);
        lead = ch - '0';
        ++string;
    } else if (len != 12)
        return SetErr(-EINVAL, "Invalid EAN13 string length %lu",
                           (unsigned long)len);

    /* Scale and calculate dimensions */
    float x_off, y_off, new_width, new_height, x_width, bar_height, bar_ext,
        font;

    pdf_barcode_eanupc_calc_dims(PDF_BARCODE_EAN13, width, height, &x_off,
                                 &y_off, &new_width, &new_height, &x_width,
                                 &bar_height, &bar_ext, &font);

    x += x_off;
    y += y_off;
    float bar_y = y + new_height - bar_height;

    int e;
    const char *save_font = pdf->current_font->font.name;
    e = pdf_set_font(pdf, "Courier"); /* Built-in monospace font */
    if (e < 0)
        return e;

    char text[2];
    text[1] = 0;
    text[0] = lead + '0';
    e = pdf_add_text(pdf, page, text, font, x, y, colour);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    x += eanupc_dimensions[0].quiet_left * x_width;
    e = pdf_barcode_eanupc_aux(pdf, page, x, bar_y - bar_ext, x_width,
                               bar_height + bar_ext, colour, GUARD_NORMAL,
                               &x);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    for (int i = 0; i != 6; i++) {
        text[0] = *string;
        e = pdf_add_text_wrap(pdf, page, text, font, x, y, 0, colour,
                              7 * x_width, PDF_ALIGN_CENTER, nullptr);
        if (e < 0) {
            pdf_set_font(pdf, save_font);
            return e;
        }

        int set = (set_ean13_encoding[lead] & 1 << i) ? 1 : 0;
        e = pdf_barcode_eanupc_ch(pdf, page, x, bar_y, x_width, bar_height,
                                  colour, *string, set, &x);
        if (e < 0) {
            pdf_set_font(pdf, save_font);
            return e;
        }
        string++;
    }

    e = pdf_barcode_eanupc_aux(pdf, page, x, bar_y - bar_ext, x_width,
                               bar_height + bar_ext, colour, GUARD_CENTRE,
                               &x);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    for (int i = 0; i != 6; i++) {
        text[0] = *string;
        e = pdf_add_text_wrap(pdf, page, text, font, x, y, 0, colour,
                              7 * x_width, PDF_ALIGN_CENTER, nullptr);
        if (e < 0) {
            pdf_set_font(pdf, save_font);
            return e;
        }

        e = pdf_barcode_eanupc_ch(pdf, page, x, bar_y, x_width, bar_height,
                                  colour, *string, 2, &x);
        if (e < 0) {
            pdf_set_font(pdf, save_font);
            return e;
        }
        string++;
    }

    e = pdf_barcode_eanupc_aux(pdf, page, x, bar_y - bar_ext, x_width,
                               bar_height + bar_ext, colour, GUARD_NORMAL,
                               &x);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    text[0] = '>';
    x += eanupc_dimensions[0].quiet_right * x_width -
         604.0f * font / (14.0f * 72.0f);
    e = pdf_add_text(pdf, page, text, font, x, y, colour);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }
    pdf_set_font(pdf, save_font);
    return 0;
}

static int pdf_add_barcode_upca(pdf_doc *pdf, Object *page,
                                float x, float y, float width, float height,
                                const char *string, uint32_t colour)
{
    if (!string)
        return 0;

    size_t len = strlen(string);
    if (len != 12)
        return SetErr(-EINVAL, "Invalid UPCA string length %lu",
                           (unsigned long)len);

    /* Scale and calculate dimensions */
    float x_off, y_off, new_width, new_height;
    float x_width, bar_height, bar_ext, font;

    pdf_barcode_eanupc_calc_dims(PDF_BARCODE_UPCA, width, height, &x_off,
                                 &y_off, &new_width, &new_height, &x_width,
                                 &bar_height, &bar_ext, &font);

    x += x_off;
    y += y_off;
    float bar_y = y + new_height - bar_height;

    int e;
    const char *save_font = pdf->current_font->font.name;
    e = pdf_set_font(pdf, "Courier");
    if (e < 0)
        return e;

    char text[2];
    text[1] = 0;
    text[0] = *string;
    e = pdf_add_text(pdf, page, text, font * 4.0f / 7.0f, x, y, colour);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    x += eanupc_dimensions[1].quiet_left * x_width;
    e = pdf_barcode_eanupc_aux(pdf, page, x, bar_y - bar_ext, x_width,
                               bar_height + bar_ext, colour, GUARD_NORMAL,
                               &x);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    for (int i = 0; i != 6; i++) {
        text[0] = *string;
        if (i) {
            e = pdf_add_text_wrap(pdf, page, text, font, x, y, 0, colour,
                                  7 * x_width, PDF_ALIGN_CENTER, nullptr);
            if (e < 0) {
                pdf_set_font(pdf, save_font);
                return e;
            }
        }

        e = pdf_barcode_eanupc_ch(pdf, page, x, bar_y - (i ? 0 : bar_ext),
                                  x_width, bar_height + (i ? 0 : bar_ext),
                                  colour, *string, 0, &x);
        if (e < 0) {
            pdf_set_font(pdf, save_font);
            return e;
        }
        string++;
    }

    e = pdf_barcode_eanupc_aux(pdf, page, x, bar_y - bar_ext, x_width,
                               bar_height + bar_ext, colour, GUARD_CENTRE,
                               &x);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    for (int i = 0; i != 6; i++) {
        text[0] = *string;
        if (i != 5) {
            e = pdf_add_text_wrap(pdf, page, text, font, x, y, 0, colour,
                                  7 * x_width, PDF_ALIGN_CENTER, nullptr);
            if (e < 0) {
                pdf_set_font(pdf, save_font);
                return e;
            }
        }

        e = pdf_barcode_eanupc_ch(
            pdf, page, x, bar_y - (i != 5 ? 0 : bar_ext), x_width,
            bar_height + (i != 5 ? 0 : bar_ext), colour, *string, 2, &x);
        if (e < 0) {
            pdf_set_font(pdf, save_font);
            return e;
        }
        string++;
    }

    e = pdf_barcode_eanupc_aux(pdf, page, x, bar_y - bar_ext, x_width,
                               bar_height + bar_ext, colour, GUARD_NORMAL,
                               &x);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    text[0] = *--string;

    x += eanupc_dimensions[1].quiet_right * x_width -
         604.0f * font * 4.0f / 7.0f / (14.0f * 72.0f);
    e = pdf_add_text(pdf, page, text, font * 4.0f / 7.0f, x, y, colour);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }
    pdf_set_font(pdf, save_font);
    return 0;
}

static int pdf_add_barcode_ean8(pdf_doc *pdf, Object *page,
                                float x, float y, float width, float height,
                                const char *string, uint32_t colour)
{
    if (!string)
        return 0;

    size_t len = strlen(string);
    if (len != 8)
        return SetErr(-EINVAL, "Invalid EAN8 string length %lu",
                           (unsigned long)len);

    /* Scale and calculate dimensions */
    float x_off, y_off, new_width, new_height, x_width, bar_height, bar_ext,
        font;

    pdf_barcode_eanupc_calc_dims(PDF_BARCODE_EAN8, width, height, &x_off,
                                 &y_off, &new_width, &new_height, &x_width,
                                 &bar_height, &bar_ext, &font);

    x += x_off;
    y += y_off;
    float bar_y = y + new_height - bar_height;

    int e;
    const char *save_font = pdf->current_font->font.name;
    e = pdf_set_font(pdf, "Courier"); /* Built-in monospace font */
    if (e < 0)
        return e;

    char text[2];
    text[1] = 0;
    text[0] = '<';
    e = pdf_add_text(pdf, page, text, font, x, y, colour);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    x += eanupc_dimensions[2].quiet_left * x_width;
    e = pdf_barcode_eanupc_aux(pdf, page, x, bar_y - bar_ext, x_width,
                               bar_height + bar_ext, colour, GUARD_NORMAL,
                               &x);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    for (int i = 0; i != 4; i++) {
        text[0] = *string;
        e = pdf_add_text_wrap(pdf, page, text, font, x, y, 0, colour,
                              7 * x_width, PDF_ALIGN_CENTER, nullptr);
        if (e < 0) {
            pdf_set_font(pdf, save_font);
            return e;
        }

        e = pdf_barcode_eanupc_ch(pdf, page, x, bar_y, x_width, bar_height,
                                  colour, *string, 0, &x);
        if (e < 0) {
            pdf_set_font(pdf, save_font);
            return e;
        }
        string++;
    }

    e = pdf_barcode_eanupc_aux(pdf, page, x, bar_y - bar_ext, x_width,
                               bar_height + bar_ext, colour, GUARD_CENTRE,
                               &x);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    for (int i = 0; i != 4; i++) {
        text[0] = *string;
        e = pdf_add_text_wrap(pdf, page, text, font, x, y, 0, colour,
                              7 * x_width, PDF_ALIGN_CENTER, nullptr);
        if (e < 0) {
            pdf_set_font(pdf, save_font);
            return e;
        }

        e = pdf_barcode_eanupc_ch(pdf, page, x, bar_y, x_width, bar_height,
                                  colour, *string, 2, &x);
        if (e < 0) {
            pdf_set_font(pdf, save_font);
            return e;
        }
        string++;
    }

    e = pdf_barcode_eanupc_aux(pdf, page, x, bar_y - bar_ext, x_width,
                               bar_height + bar_ext, colour, GUARD_NORMAL,
                               &x);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    text[0] = '>';
    x += eanupc_dimensions[0].quiet_right * x_width -
         604.0f * font / (14.0f * 72.0f);
    e = pdf_add_text(pdf, page, text, font, x, y, colour);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }
    pdf_set_font(pdf, save_font);
    return 0;
}

static int pdf_add_barcode_upce(pdf_doc *pdf, Object *page,
                                float x, float y, float width, float height,
                                const char *string, uint32_t colour)
{
    if (!string)
        return 0;

    size_t len = strlen(string);
    if (len != 12)
        return SetErr(-EINVAL, "Invalid UPCE string length %lu",
                           (unsigned long)len);

    if (*string != '0')
        return SetErr(-EINVAL, "Invalid UPCE char %c at start",
                           *string);

    for (size_t i = 0; i < len; i++) {
        if (!isdigit(string[i]))
            return SetErr(-EINVAL, "Invalid UPCE char 0x%x at %lu",
                               string[i], (unsigned long)i);
    }

    /* Scale and calculate dimensions */
    float x_off, y_off, new_width, new_height;
    float x_width, bar_height, bar_ext, font;

    pdf_barcode_eanupc_calc_dims(PDF_BARCODE_UPCE, width, height, &x_off,
                                 &y_off, &new_width, &new_height, &x_width,
                                 &bar_height, &bar_ext, &font);

    x += x_off;
    y += y_off;
    float bar_y = y + new_height - bar_height;

    int e;
    const char *save_font = pdf->current_font->font.name;
    e = pdf_set_font(pdf, "Courier");
    if (e < 0)
        return e;

    char text[2];
    text[1] = 0;
    text[0] = string[0];
    e = pdf_add_text(pdf, page, text, font * 4.0f / 7.0f, x, y, colour);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    x += eanupc_dimensions[2].quiet_left * x_width;
    e = pdf_barcode_eanupc_aux(pdf, page, x, bar_y, x_width, bar_height,
                               colour, GUARD_NORMAL, &x);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    char X[6];
    if (string[5] && memcmp(string + 6, "0000", 4) == 0 &&
        '5' <= string[10] && string[10] <= '9') {
        memcpy(X, string + 1, 5);
        X[5] = string[10];
    } else if (string[4] && memcmp(string + 5, "00000", 5) == 0) {
        memcpy(X, string + 1, 4);
        X[4] = string[11];
        X[5] = 4;
    } else if ('0' <= string[3] && string[3] <= '2' &&
               memcmp(string + 4, "0000", 4) == 0) {
        X[0] = string[1];
        X[1] = string[2];
        X[2] = string[8];
        X[3] = string[9];
        X[4] = string[10];
        X[5] = string[3];
    } else if ('3' <= string[3] && string[3] <= '9' &&
               memcmp(string + 4, "00000", 5) == 0) {
        memcpy(X, string + 1, 3);
        X[3] = string[9];
        X[4] = string[10];
        X[5] = 3;
    } else {
        pdf_set_font(pdf, save_font);
        return SetErr(-EINVAL, "Invalid UPCE string format");
    }

    for (int i = 0; i != 6; i++) {
        text[0] = X[i];
        e = pdf_add_text_wrap(pdf, page, text, font, x, y, 0, colour,
                              7 * x_width, PDF_ALIGN_CENTER, nullptr);
        if (e < 0) {
            pdf_set_font(pdf, save_font);
            return e;
        }

        int set = (set_upce_encoding[string[11] - '0'] & 1 << i) ? 1 : 0;
        e = pdf_barcode_eanupc_ch(pdf, page, x, bar_y, x_width, bar_height,
                                  colour, X[i], set, &x);
        if (e < 0) {
            pdf_set_font(pdf, save_font);
            return e;
        }
    }

    e = pdf_barcode_eanupc_aux(pdf, page, x, bar_y, x_width, bar_height,
                               colour, GUARD_SPECIAL, &x);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    text[0] = string[11];
    x += eanupc_dimensions[0].quiet_right * x_width -
         604.0f * font * 4.0f / 7.0f / (14.0f * 72.0f);
    e = pdf_add_text(pdf, page, text, font * 4.0f / 7.0f, x, y, colour);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    pdf_set_font(pdf, save_font);
    return 0;
}

int pdf_add_barcode(pdf_doc *pdf, Object *page, int code,
                    float x, float y, float width, float height,
                    const char *string, uint32_t colour)
{
    if (!string || !*string)
        return 0;
    switch (code) {
    case PDF_BARCODE_128A:
        return pdf_add_barcode_128a(pdf, page, x, y, width, height, string,
                                    colour);
    case PDF_BARCODE_39:
        return pdf_add_barcode_39(pdf, page, x, y, width, height, string,
                                  colour);
    case PDF_BARCODE_EAN13:
        return pdf_add_barcode_ean13(pdf, page, x, y, width, height, string,
                                     colour);
    case PDF_BARCODE_UPCA:
        return pdf_add_barcode_upca(pdf, page, x, y, width, height, string,
                                    colour);
    case PDF_BARCODE_EAN8:
        return pdf_add_barcode_ean8(pdf, page, x, y, width, height, string,
                                    colour);
    case PDF_BARCODE_UPCE:
        return pdf_add_barcode_upce(pdf, page, x, y, width, height, string,
                                    colour);
    default:
        return SetErr(-EINVAL, "Invalid barcode code %d", code);
    }
}

static Object *pdf_add_raw_grayscale8(pdf_doc *pdf,
                                          const uint8_t *data, uint32_t width,
                                          uint32_t height)
{
    Object *obj;
    size_t len;
    const char *endstream = ">\r\nendstream\r\n";
    dstr str = INIT_DSTR;
    size_t data_len = (size_t)width * (size_t)height;

    dstr_printf(&str,
                "<<\r\n"
                "  /Type /XObject\r\n"
                "  /Name /Image%d\r\n"
                "  /Subtype /Image\r\n"
                "  /ColorSpace /DeviceGray\r\n"
                "  /Height %d\r\n"
                "  /Width %d\r\n"
                "  /BitsPerComponent 8\r\n"
                "  /Length %lu\r\n"
                ">>stream\r\n",
                flexarray_size(&pdf->objects), height, width,
                (unsigned long)(data_len + 1));

    len = dstr_len(&str) + data_len + strlen(endstream) + 1;
    if (dstr_ensure(&str, len) < 0) {
        dstr_free(&str);
        SetErr(-ENOMEM,
                    "Unable to allocate %lu bytes memory for image",
                    (unsigned long)len);
        return nullptr;
    }
    dstr_append_data(&str, data, data_len);
    dstr_append(&str, endstream);

    obj = pdf_add_object(pdf, OBJ_image);
    if (!obj) {
        dstr_free(&str);
        return nullptr;
    }
    obj->stream.stream = str;

    return obj;
}

static Object *pdf_add_raw_rgb24(pdf_doc *pdf,
                                            const uint8_t *data,
                                            uint32_t width, uint32_t height)
{
    Object *obj;
    size_t len;
    const char *endstream = ">\r\nendstream\r\n";
    dstr str = INIT_DSTR;
    size_t data_len = (size_t)width * (size_t)height * 3;

    dstr_printf(&str,
                "<<\r\n"
                "  /Type /XObject\r\n"
                "  /Name /Image%d\r\n"
                "  /Subtype /Image\r\n"
                "  /ColorSpace /DeviceRGB\r\n"
                "  /Height %d\r\n"
                "  /Width %d\r\n"
                "  /BitsPerComponent 8\r\n"
                "  /Length %lu\r\n"
                ">>stream\r\n",
                flexarray_size(&pdf->objects), height, width,
                (unsigned long)(data_len + 1));

    len = dstr_len(&str) + data_len + strlen(endstream) + 1;
    if (dstr_ensure(&str, len) < 0) {
        dstr_free(&str);
        SetErr(-ENOMEM,
                    "Unable to allocate %lu bytes memory for image",
                    (unsigned long)len);
        return nullptr;
    }
    dstr_append_data(&str, data, data_len);
    dstr_append(&str, endstream);

    obj = pdf_add_object(pdf, OBJ_image);
    if (!obj) {
        dstr_free(&str);
        return nullptr;
    }
    obj->stream.stream = str;

    return obj;
}

static uint8_t *get_file(pdf_doc *pdf, const char *file_name,
                         size_t *length)
{
    FILE *fp;
    uint8_t *file_data;
    struct stat buf;
    off_t len;

    if ((fp = fopen(file_name, "rb")) == nullptr) {
        SetErr(-errno, "Unable to open %s: %s", file_name,
                    strerror(errno));
        return nullptr;
    }

    if (fstat(fileno(fp), &buf) < 0) {
        SetErr(-errno, "Unable to access %s: %s", file_name,
                    strerror(errno));
        fclose(fp);
        return nullptr;
    }

    len = buf.st_size;

    file_data = (uint8_t *)malloc(len);
    if (!file_data) {
        SetErr(-ENOMEM, "Unable to allocate: %d", (int)len);
        fclose(fp);
        return nullptr;
    }

    if (fread(file_data, len, 1, fp) != 1) {
        SetErr(-errno, "Unable to read full data: %s", file_name);
        free(file_data);
        fclose(fp);
        return nullptr;
    }

    fclose(fp);

    *length = len;

    return file_data;
}

static Object *
pdf_add_raw_jpeg_data(pdf_doc *pdf, const struct pdf_img_info *info,
                      const uint8_t *jpeg_data, size_t len)
{
    Object *obj = pdf_add_object(pdf, OBJ_image);
    if (!obj)
        return nullptr;

    dstr_printf(&obj->stream.stream,
                "<<\r\n"
                "  /Type /XObject\r\n"
                "  /Name /Image%d\r\n"
                "  /Subtype /Image\r\n"
                "  /ColorSpace %s\r\n"
                "  /Width %d\r\n"
                "  /Height %d\r\n"
                "  /BitsPerComponent 8\r\n"
                "  /Filter /DCTDecode\r\n"
                "  /Length %lu\r\n"
                ">>stream\r\n",
                flexarray_size(&pdf->objects),
                (info->jpeg.ncolours == 1) ? "/DeviceGray" : "/DeviceRGB",
                info->width, info->height, (unsigned long) len);
    dstr_append_data(&obj->stream.stream, jpeg_data, len);

    dstr_printf(&obj->stream.stream, "\r\nendstream\r\n");

    return obj;
}

/**
 * Get the display dimensions of an image, respecting the images aspect ratio
 * if only one desired display dimension is defined.
 * The pdf parameter is only used for setting the error value.
 */
static int get_img_display_dimensions(pdf_doc *pdf, uint32_t img_width,
                                      uint32_t img_height,
                                      float *display_width,
                                      float *display_height)
{
    if (!display_height || !display_width) {
        return SetErr(
            -EINVAL,
            "display_width and display_height may not be null pointers");
    }

    const float display_width_in = *display_width;
    const float display_height_in = *display_height;

    if (display_width_in < 0 && display_height_in < 0) {
        return SetErr(-EINVAL,
                           "Unable to determine image display dimensions, "
                           "display_width and display_height are both < 0");
    }
    if (img_width == 0 || img_height == 0) {
        return SetErr(-EINVAL,
                           "Invalid image dimensions received, the loaded "
                           "image appears to be empty.");
    }

    if (display_width_in < 0) {
        // Set width, keeping aspect ratio
        *display_width = display_height_in * ((float)img_width / img_height);
    } else if (display_height_in < 0) {
        // Set height, keeping aspect ratio
        *display_height = display_width_in * ((float)img_height / img_width);
    }
    return 0;
}

static int pdf_add_image(pdf_doc *pdf, Object *page,
                         Object *image, float x, float y,
                         float width, float height)
{
    int ret;
    dstr str = INIT_DSTR;

    if (!page)
        page = pdf_find_last_object(pdf, OBJ_page);

    if (!page)
        return SetErr(-EINVAL, "Invalid pdf page");

    if (image->type != OBJ_image)
        return SetErr(-EINVAL,
                           "adding an image, but wrong object type %d",
                           image->type);

    if (image->stream.page != nullptr)
        return SetErr(-EEXIST, "image already on a page");

    image->stream.page = page;

    dstr_append(&str, "q ");
    dstr_printf(&str, "%f 0 0 %f %f %f cm ", width, height, x, y);
    dstr_printf(&str, "/Image%d Do ", image->index);
    dstr_append(&str, "Q");

    ret = pdf_add_stream(pdf, page, dstr_data(&str));
    dstr_free(&str);
    return ret;
}


// Works like fgets, except it's for a fixed in-memory buffer of data
static size_t dgets(const uint8_t *data, size_t *pos, size_t len, char *line,
                    size_t line_len)
{
    size_t line_pos = 0;

    if (*pos >= len)
        return 0;

    while ((*pos) < len) {
        if (line_pos < line_len) {
            // Reject non-ascii data
            if (data[*pos] & 0x80) {
                return 0;
            }
            line[line_pos] = data[*pos];
            line_pos++;
        }
        if (data[*pos] == '\n') {
            (*pos)++;
            break;
        }
        (*pos)++;
    }

    if (line_pos < line_len) {
        line[line_pos] = '\0';
    }

    return *pos;
}

#endif

#if 0

static int parse_ppm_header(struct pdf_img_info *info, const uint8_t *data,
                            size_t length, char *err_msg,
                            size_t err_msg_length)
{
    char line[1024];
    memset(line, '\0', sizeof(line));
    size_t pos = 0;

    // Load the PPM file
    if (!dgets(data, &pos, length, line, sizeof(line) - 1)) {
        snprintf(err_msg, err_msg_length, "Invalid PPM file");
        return -EINVAL;
    }

    // Determine number of color channels (Also, we only support binary ppms)
    int ncolors;
    if (strncmp(line, "P6", 2) == 0) {
        info->ppm.color_space = PPM_BINARY_COLOR_RGB;
        ncolors = 3;
    } else if (strncmp(line, "P5", 2) == 0) {
        info->ppm.color_space = PPM_BINARY_COLOR_GRAY;
        ncolors = 1;
    } else {
        snprintf(err_msg, err_msg_length,
                 "Only binary PPM files (grayscale, RGB) supported");
        return -EINVAL;
    }

    // Skip comments before header
    do {
        if (!dgets(data, &pos, length, line, sizeof(line) - 1)) {
            snprintf(err_msg, err_msg_length, "Unable to find PPM header");
            return -EINVAL;
        }
    } while (line[0] == '#');

    // Read image dimensions
    if (sscanf(line, "%u %u\n", &(info->width), &(info->height)) != 2) {
        snprintf(err_msg, err_msg_length, "Unable to find PPM size");
        return -EINVAL;
    }
    if (info->width > MAX_IMAGE_WIDTH || info->height > MAX_IMAGE_HEIGHT) {
        snprintf(err_msg, err_msg_length, "Invalid width/height: %ux%u",
                 info->width, info->height);
        return -EINVAL;
    }
    info->ppm.size = (size_t)(info->width * info->height * ncolors);
    info->ppm.data_begin_pos = pos;

    return 0;
}

static int pdf_add_ppm_data(pdf_doc *pdf, Object *page,
                            float x, float y, float display_width,
                            float display_height,
                            const struct pdf_img_info *info,
                            const uint8_t *ppm_data, size_t len)
{
    char line[1024];
    // We start reading at the position delivered by parse_ppm_header,
    // since we already parsed the header of the file there.
    size_t pos = info->ppm.data_begin_pos;

    /* Skip over the byte-size line */
    if (!dgets(ppm_data, &pos, len, line, sizeof(line) - 1))
        return SetErr(-EINVAL, "No byte-size line in PPM file");

    /* Try and limit the memory usage to sane images */
    if (info->width > MAX_IMAGE_WIDTH || info->height > MAX_IMAGE_HEIGHT) {
        return SetErr(-EINVAL,
                           "Invalid width/height in PPM file: %ux%u",
                           info->width, info->height);
    }

    if (info->ppm.size > len - pos) {
        return SetErr(-EINVAL, "Insufficient image data available");
    }

    switch (info->ppm.color_space) {
    case PPM_BINARY_COLOR_GRAY:
        return pdf_add_grayscale8(pdf, page, x, y, display_width,
                                  display_height, &ppm_data[pos], info->width,
                                  info->height);
        break;

    case PPM_BINARY_COLOR_RGB:
        return pdf_add_rgb24(pdf, page, x, y, display_width, display_height,
                             &ppm_data[pos], info->width, info->height);
        break;

    default:
        return SetErr(-EINVAL,
                           "Invalid color space in ppm file: %i",
                           info->ppm.color_space);
        break;
    }
}

static int parse_jpeg_header(struct pdf_img_info *info, const uint8_t *data,
                             size_t length, char *err_msg,
                             size_t err_msg_length)
{
    // See http://www.videotechnology.com/jpeg/j1.html for details
    if (length >= 4 && data[0] == 0xFF && data[1] == 0xD8) {
        for (size_t i = 2; i < length; i++) {
            if (data[i] != 0xff) {
                break;
            }
            while (++i < length && data[i] == 0xff)
                ;
            if (i + 2 >= length) {
                break;
            }
            int len = data[i + 1] * 256 + data[i + 2];
            /* Search for SOFn marker and decode jpeg details */
            if ((data[i] & 0xf4) == 0xc0) {
                if (len >= 9 && i + len + 1 < length) {
                    info->height = data[i + 4] * 256 + data[i + 5];
                    info->width = data[i + 6] * 256 + data[i + 7];
                    info->jpeg.ncolours = data[i + 8];
                    return 0;
                }
                break;
            }
            i += len;
        }
    }
    snprintf(err_msg, err_msg_length, "Error parsing JPEG header");
    return -EINVAL;
}

static int pdf_add_jpeg_data(pdf_doc *pdf, Object *page,
                             float x, float y, float display_width,
                             float display_height, struct pdf_img_info *info,
                             const uint8_t *jpeg_data, size_t len)
{
    Object *obj;

    obj = pdf_add_raw_jpeg_data(pdf, info, jpeg_data, len);
    if (!obj)
        return pdf->errval;

    if (get_img_display_dimensions(pdf, info->width, info->height,
                                   &display_width, &display_height)) {
        return pdf->errval;
    }
    return pdf_add_image(pdf, page, obj, x, y, display_width, display_height);
}

int pdf_add_rgb24(pdf_doc *pdf, Object *page, float x,
                  float y, float display_width, float display_height,
                  const uint8_t *data, uint32_t width, uint32_t height)
{
    Object *obj;

    obj = pdf_add_raw_rgb24(pdf, data, width, height);
    if (!obj)
        return pdf->errval;

    if (get_img_display_dimensions(pdf, width, height, &display_width,
                                   &display_height)) {
        return pdf->errval;
    }
    return pdf_add_image(pdf, page, obj, x, y, display_width, display_height);
}

int pdf_add_grayscale8(pdf_doc *pdf, Object *page, float x,
                       float y, float display_width, float display_height,
                       const uint8_t *data, uint32_t width, uint32_t height)
{
    Object *obj;

    obj = pdf_add_raw_grayscale8(pdf, data, width, height);
    if (!obj)
        return pdf->errval;

    if (get_img_display_dimensions(pdf, width, height, &display_width,
                                   &display_height)) {
        return pdf->errval;
    }
    return pdf_add_image(pdf, page, obj, x, y, display_width, display_height);
}

static int parse_png_header(struct pdf_img_info *info, const uint8_t *data,
                            size_t length, char *err_msg,
                            size_t err_msg_length)
{
    if (length <= sizeof(png_signature)) {
        snprintf(err_msg, err_msg_length, "PNG file too short");
        return -EINVAL;
    }

    if (memcmp(data, png_signature, sizeof(png_signature))) {
        snprintf(err_msg, err_msg_length, "File is not correct PNG file");
        return -EINVAL;
    }

    // process first PNG chunk
    uint32_t pos = sizeof(png_signature);
    const struct png_chunk *chunk = (const struct png_chunk *)&data[pos];
    pos += sizeof(struct png_chunk);
    if (pos > length) {
        snprintf(err_msg, err_msg_length, "PNG file too short");
        return -EINVAL;
    }
    if (strncmp(chunk->type, png_chunk_header, 4) == 0) {
        // header found, process width and height, check errors
        struct png_header *header = &info->png;

        if (pos + sizeof(struct png_header) > length) {
            snprintf(err_msg, err_msg_length, "PNG file too short");
            return -EINVAL;
        }

        memcpy(header, &data[pos], sizeof(struct png_header));
        if (header->deflate != 0) {
            snprintf(err_msg, err_msg_length, "Deflate wrong in PNG header");
            return -EINVAL;
        }
        if (header->bitDepth == 0) {
            snprintf(err_msg, err_msg_length, "PNG file has zero bit depth");
            return -EINVAL;
        }
        // ensure the width and height values have the proper byte order
        // and copy them into the info struct.
        header->width = ntoh32(header->width);
        header->height = ntoh32(header->height);
        info->width = header->width;
        info->height = header->height;
        return 0;
    }
    snprintf(err_msg, err_msg_length, "Failed to read PNG file header");
    return -EINVAL;
}

static int pdf_add_png_data(pdf_doc *pdf, Object *page,
                            float x, float y, float display_width,
                            float display_height,
                            const struct pdf_img_info *img_info,
                            const uint8_t *png_data, size_t png_data_length)
{
    // indicates if we return an error or add the img at the
    // end of the function
    bool success = false;

    // string stream used for writing color space (and palette) info
    // into the pdf
    dstr colour_space = INIT_DSTR;

    Object *obj = nullptr;
    uint8_t *final_data = nullptr;
    int written = 0;
    uint32_t pos;
    uint8_t *png_data_temp = nullptr;
    size_t png_data_total_length = 0;
    uint8_t ncolours;

    // Stores palette information for indexed PNGs
    struct rgb_value *palette_buffer = nullptr;
    size_t palette_buffer_length = 0;

    const struct png_header *header = &img_info->png;

    // Father info from png header
    switch (header->colorType) {
    case PNG_COLOR_GREYSCALE:
        ncolours = 1;
        break;
    case PNG_COLOR_RGB:
        ncolours = 3;
        break;
    case PNG_COLOR_INDEXED:
        ncolours = 1;
        break;
    // PNG_COLOR_RGBA and PNG_COLOR_GREYSCALE_A are unsupported
    default:
        SetErr(-EINVAL, "PNG has unsupported color type: %d",
                    header->colorType);
        goto free_buffers;
        break;
    }

    /* process PNG chunks */
    pos = sizeof(png_signature);

    while (1) {
        const struct png_chunk *chunk;

        chunk = (const struct png_chunk *)&png_data[pos];
        pos += sizeof(struct png_chunk);

        if (pos > png_data_length - 4) {
            SetErr(-EINVAL, "PNG file too short");
            goto free_buffers;
        }
        const uint32_t chunk_length = ntoh32(chunk->length);
        // chunk length + 4-bytes of CRC
        if (chunk_length > png_data_length - pos - 4) {
            SetErr(-EINVAL, "PNG chunk exceeds file: %d vs %ld",
                        chunk_length, (long)(png_data_length - pos - 4));
            goto free_buffers;
        }
        if (strncmp(chunk->type, png_chunk_header, 4) == 0) {
            // Ignoring the header, since it was parsed
            // before calling this function.
        } else if (strncmp(chunk->type, png_chunk_palette, 4) == 0) {
            // Palette chunk
            if (header->colorType == PNG_COLOR_INDEXED) {
                // palette chunk is needed for indexed images
                if (palette_buffer) {
                    SetErr(-EINVAL,
                                "PNG contains multiple palette chunks");
                    goto free_buffers;
                }
                if (chunk_length % 3 != 0) {
                    SetErr(-EINVAL,
                                "PNG format error: palette chunk length is "
                                "not divisbly by 3!");
                    goto free_buffers;
                }
                palette_buffer_length = (size_t)(chunk_length / 3);
                if (palette_buffer_length > 256 ||
                    palette_buffer_length == 0) {
                    SetErr(-EINVAL,
                                "PNG palette length invalid: %lu",
                                (unsigned long)palette_buffer_length);
                    goto free_buffers;
                }
                palette_buffer = (struct rgb_value *)malloc(
                    palette_buffer_length * sizeof(struct rgb_value));
                if (!palette_buffer) {
                    SetErr(-ENOMEM,
                                "Could not allocate memory for indexed color "
                                "palette (%lu bytes)",
                                (unsigned long)(palette_buffer_length *
                                                sizeof(struct rgb_value)));
                    goto free_buffers;
                }
                for (size_t i = 0; i < palette_buffer_length; i++) {
                    size_t offset = (i * 3) + pos;
                    palette_buffer[i].red = png_data[offset];
                    palette_buffer[i].green = png_data[offset + 1];
                    palette_buffer[i].blue = png_data[offset + 2];
                }
            } else if (header->colorType == PNG_COLOR_RGB ||
                       header->colorType == PNG_COLOR_RGBA) {
                // palette chunk is optional for RGB(A) images
                // but we do not process them
            } else {
                SetErr(-EINVAL,
                            "Unexpected palette chunk for color type %d",
                            header->colorType);
                goto free_buffers;
            }
        } else if (strncmp(chunk->type, png_chunk_data, 4) == 0) {
            if (chunk_length > 0 && chunk_length < png_data_length - pos) {
                uint8_t *data = (uint8_t *)realloc(
                    png_data_temp, png_data_total_length + chunk_length);
                // (uint8_t *)realloc(info.data, info.length + chunk_length);
                if (!data) {
                    SetErr(-ENOMEM, "No memory for PNG data");
                    goto free_buffers;
                }
                png_data_temp = data;
                memcpy(&png_data_temp[png_data_total_length], &png_data[pos],
                       chunk_length);
                png_data_total_length += chunk_length;
            }
        } else if (strncmp(chunk->type, png_chunk_end, 4) == 0) {
            /* end of file, exit */
            break;
        }

        if (chunk_length >= png_data_length) {
            SetErr(-EINVAL, "PNG chunk length larger than file");
            goto free_buffers;
        }

        pos += chunk_length;     // add chunk length
        pos += sizeof(uint32_t); // add CRC length
    }

    /* if no length was found */
    if (png_data_total_length == 0) {
        SetErr(-EINVAL, "PNG file has zero length");
        goto free_buffers;
    }

    switch (header->colorType) {
    case PNG_COLOR_GREYSCALE:
        dstr_append(&colour_space, "/DeviceGray");
        break;
    case PNG_COLOR_RGB:
        dstr_append(&colour_space, "/DeviceRGB");
        break;
    case PNG_COLOR_INDEXED:
        if (palette_buffer_length == 0) {
            SetErr(-EINVAL, "Indexed PNG contains no palette");
            goto free_buffers;
        }
        // Write the color palette to the color_palette buffer
        dstr_printf(&colour_space,
                    "[ /Indexed\r\n"
                    "  /DeviceRGB\r\n"
                    "  %lu\r\n"
                    "  <",
                    (unsigned long)(palette_buffer_length - 1));
        // write individual paletter values
        // the index value for every RGB value is determined by its position
        // (0, 1, 2, ...)
        for (size_t i = 0; i < palette_buffer_length; i++) {
            dstr_printf(&colour_space, "%02X%02X%02X ", palette_buffer[i].red,
                        palette_buffer[i].green, palette_buffer[i].blue);
        }
        dstr_append(&colour_space, ">\r\n]");
        break;

    default:
        SetErr(-EINVAL,
                    "Cannot map PNG color type %d to PDF color space",
                    header->colorType);
        goto free_buffers;
        break;
    }

    final_data = (uint8_t *)malloc(png_data_total_length + 1024 +
                                   dstr_len(&colour_space));
    if (!final_data) {
        SetErr(-ENOMEM, "Unable to allocate PNG data %lu",
                    (unsigned long)(png_data_total_length + 1024 +
                                    dstr_len(&colour_space)));
        goto free_buffers;
    }

    // Write image information to PDF
    written =
        sprintf((char *)final_data,
                "<<\r\n"
                "  /Type /XObject\r\n"
                "  /Name /Image%d\r\n"
                "  /Subtype /Image\r\n"
                "  /ColorSpace %s\r\n"
                "  /Width %u\r\n"
                "  /Height %u\r\n"
                "  /Interpolate true\r\n"
                "  /BitsPerComponent %u\r\n"
                "  /Filter /FlateDecode\r\n"
                "  /DecodeParms << /Predictor 15 /Colors %d "
                "/BitsPerComponent %u /Columns %u >>\r\n"
                "  /Length %zu\r\n"
                ">>stream\r\n",
                flexarray_size(&pdf->objects), dstr_data(&colour_space),
                header->width, header->height, header->bitDepth, ncolours,
                header->bitDepth, header->width, png_data_total_length);

    memcpy(&final_data[written], png_data_temp, png_data_total_length);
    written += png_data_total_length;
    written += sprintf((char *)&final_data[written], "\r\nendstream\r\n");

    obj = pdf_add_object(pdf, OBJ_image);
    if (!obj) {
        goto free_buffers;
    }

    dstr_append_data(&obj->stream.stream, final_data, written);

    if (get_img_display_dimensions(pdf, header->width, header->height,
                                   &display_width, &display_height)) {
        goto free_buffers;
    }
    success = true;

free_buffers:
    if (final_data)
        free(final_data);
    if (palette_buffer)
        free(palette_buffer);
    if (png_data_temp)
        free(png_data_temp);
    dstr_free(&colour_space);

    if (success)
        return pdf_add_image(pdf, page, obj, x, y, display_width,
                             display_height);
    else
        return pdf->errval;
}

static int parse_bmp_header(struct pdf_img_info *info, const uint8_t *data,
                            size_t data_length, char *err_msg,
                            size_t err_msg_length)
{
    if (data_length < sizeof(bmp_signature) + sizeof(struct bmp_header)) {
        snprintf(err_msg, err_msg_length, "File is too short");
        return -EINVAL;
    }

    if (memcmp(data, bmp_signature, sizeof(bmp_signature))) {
        snprintf(err_msg, err_msg_length, "File is not correct BMP file");
        return -EINVAL;
    }
    memcpy(&info->bmp, &data[sizeof(bmp_signature)],
           sizeof(struct bmp_header));
    if (info->bmp.biWidth < 0) {
        snprintf(err_msg, err_msg_length, "BMP has negative width");
        return -EINVAL;
    }
    if (info->bmp.biHeight == INT_MIN) {
        snprintf(err_msg, err_msg_length, "BMP height overflow");
        return -EINVAL;
    }
    info->width = info->bmp.biWidth;
    // biHeight might be negative (positive indicates vertically mirrored
    // lines)
    info->height = abs(info->bmp.biHeight);
    return 0;
}

static int pdf_add_bmp_data(pdf_doc *pdf, Object *page,
                            float x, float y, float display_width,
                            float display_height,
                            const struct pdf_img_info *info,
                            const uint8_t *data, const size_t len)
{
    const struct bmp_header *header = &info->bmp;
    uint8_t *bmp_data = nullptr;
    uint8_t row_padding;
    uint32_t bpp;
    size_t data_len;
    int retval;
    const uint32_t width = info->width;
    const uint32_t height = info->height;

    if (header->bfSize != len)
        return SetErr(-EINVAL,
                           "BMP file seems to have wrong length");
    if (header->biSize != 40)
        return SetErr(-EINVAL, "Wrong BMP header: biSize");
    if (header->biCompression != 0)
        return SetErr(-EINVAL, "Wrong BMP compression value: %d",
                           header->biCompression);
    if (header->biWidth > MAX_IMAGE_WIDTH || header->biWidth <= 0 ||
        width > MAX_IMAGE_WIDTH || width == 0)
        return SetErr(-EINVAL, "BMP has invalid width: %d",
                           header->biWidth);
    if (header->biHeight > MAX_IMAGE_HEIGHT ||
        header->biHeight < -MAX_IMAGE_HEIGHT || header->biHeight == 0 ||
        height > MAX_IMAGE_HEIGHT || height == 0)
        return SetErr(-EINVAL, "BMP has invalid height: %d",
                           header->biHeight);
    if (header->biBitCount != 24 && header->biBitCount != 32)
        return SetErr(-EINVAL, "Unsupported BMP bitdepth: %d",
                           header->biBitCount);
    bpp = header->biBitCount / 8;
    /* BMP rows are 4-bytes padded! */
    row_padding = (width * bpp) & 3;
    data_len = (size_t)width * (size_t)height * 3;

    if (header->bfOffBits >= len)
        return SetErr(-EINVAL, "Invalid BMP image offset");

    if (len - header->bfOffBits <
        (size_t)height * (width + row_padding) * bpp)
        return SetErr(-EINVAL, "Wrong BMP image size");

    if (bpp == 3) {
        /* 24 bits: change R and B colors */
        bmp_data = (uint8_t *)malloc(data_len);
        if (!bmp_data)
            return SetErr(-ENOMEM,
                               "Insufficient memory for bitmap");
        for (uint32_t pos = 0; pos < width * height; pos++) {
            uint32_t src_pos =
                header->bfOffBits + 3 * (pos + (pos / width) * row_padding);

            bmp_data[pos * 3] = data[src_pos + 2];
            bmp_data[pos * 3 + 1] = data[src_pos + 1];
            bmp_data[pos * 3 + 2] = data[src_pos];
        }
    } else if (bpp == 4) {
        /* 32 bits: change R and B colors, remove key color */
        int offs = 0;
        bmp_data = (uint8_t *)malloc(data_len);
        if (!bmp_data)
            return SetErr(-ENOMEM,
                               "Insufficient memory for bitmap");

        for (uint32_t pos = 0; pos < width * height * 4; pos += 4) {
            bmp_data[offs] = data[header->bfOffBits + pos + 2];
            bmp_data[offs + 1] = data[header->bfOffBits + pos + 1];
            bmp_data[offs + 2] = data[header->bfOffBits + pos];
            offs += 3;
        }
    } else {
        return SetErr(-EINVAL, "Unsupported BMP bitdepth: %d",
                           header->biBitCount);
    }
    if (header->biHeight >= 0) {
        // BMP has vertically mirrored representation of lines, so swap them
        uint8_t *line = (uint8_t *)malloc(width * 3);
        if (!line) {
            free(bmp_data);
            return SetErr(-ENOMEM,
                               "Unable to allocate memory for bitmap mirror");
        }
        for (uint32_t pos = 0; pos < (height / 2); pos++) {
            memcpy(line, &bmp_data[pos * width * 3], width * 3);
            memcpy(&bmp_data[pos * width * 3],
                   &bmp_data[(height - pos - 1) * width * 3], width * 3);
            memcpy(&bmp_data[(height - pos - 1) * width * 3], line,
                   width * 3);
        }
        free(line);
    }

    retval = pdf_add_rgb24(pdf, page, x, y, display_width, display_height,
                           bmp_data, width, height);
    free(bmp_data);

    return retval;
}

static int determine_image_format(const uint8_t *data, size_t length)
{
    if (length >= sizeof(png_signature) &&
        memcmp(data, png_signature, sizeof(png_signature)) == 0)
        return IMAGE_PNG;
    if (length >= sizeof(bmp_signature) &&
        memcmp(data, bmp_signature, sizeof(bmp_signature)) == 0)
        return IMAGE_BMP;
    if (length >= sizeof(jpeg_signature) &&
        memcmp(data, jpeg_signature, sizeof(jpeg_signature)) == 0)
        return IMAGE_JPG;
    if (length >= sizeof(ppm_signature) &&
        memcmp(data, ppm_signature, sizeof(ppm_signature)) == 0)
        return IMAGE_PPM;
    if (length >= sizeof(pgm_signature) &&
        memcmp(data, pgm_signature, sizeof(pgm_signature)) == 0)
        return IMAGE_PPM;

    return IMAGE_UNKNOWN;
}

int pdf_parse_image_header(struct pdf_img_info *info, const uint8_t *data,
                           size_t length, char *err_msg,
                           size_t err_msg_length)

{
    const int image_format = determine_image_format(data, length);
    info->image_format = image_format;
    switch (image_format) {
    case IMAGE_PNG:
        return parse_png_header(info, data, length, err_msg, err_msg_length);
    case IMAGE_BMP:
        return parse_bmp_header(info, data, length, err_msg, err_msg_length);
    case IMAGE_JPG:
        return parse_jpeg_header(info, data, length, err_msg, err_msg_length);
    case IMAGE_PPM:
        return parse_ppm_header(info, data, length, err_msg, err_msg_length);

    case IMAGE_UNKNOWN:
    default:
        snprintf(err_msg, err_msg_length, "Unknown file format");
        return -EINVAL;
    }
}

int pdf_add_image_data(pdf_doc *pdf, Object *page, float x,
                       float y, float display_width, float display_height,
                       const uint8_t *data, size_t len)
{
    struct pdf_img_info info = {
        .image_format = IMAGE_UNKNOWN,
        .width = 0,
        .height = 0,
        .jpeg = {0},
    };

    int ret = pdf_parse_image_header(&info, data, len, pdf->errstr,
                                     sizeof(pdf->errstr));
    if (ret)
        return ret;

    // Try and determine which image format it is based on the content
    switch (info.image_format) {
    case IMAGE_PNG:
        return pdf_add_png_data(pdf, page, x, y, display_width,
                                display_height, &info, data, len);
    case IMAGE_BMP:
        return pdf_add_bmp_data(pdf, page, x, y, display_width,
                                display_height, &info, data, len);
    case IMAGE_JPG:
        return pdf_add_jpeg_data(pdf, page, x, y, display_width,
                                 display_height, &info, data, len);
    case IMAGE_PPM:
        return pdf_add_ppm_data(pdf, page, x, y, display_width,
                                display_height, &info, data, len);

    // This case should be caught in parse_image_header, but is checked
    // here again for safety
    case IMAGE_UNKNOWN:
    default:
        return SetErr(-EINVAL, "Unable to determine image format");
    }
}

int pdf_add_image_file(pdf_doc *pdf, Object *page, float x,
                       float y, float display_width, float display_height,
                       const char *image_filename)
{
    size_t len;
    uint8_t *data;
    int ret = 0;

    data = get_file(pdf, image_filename, &len);
    if (data == nullptr)
        return pdf_get_errval(pdf);

    ret = pdf_add_image_data(pdf, page, x, y, display_width, display_height,
                             data, len);
    free(data);
    return ret;
}

#endif


/**
 * PDF HINTS & TIPS
 * The specification can be found at
 * https://www.adobe.com/content/dam/acom/en/devnet/pdf/pdfs/pdf_reference_archives/PDFReference.pdf
 * The following sites have various bits & pieces about PDF document
 * generation
 * http://www.mactech.com/articles/mactech/Vol.15/15.09/PDFIntro/index.html
 * http://gnupdf.org/Introduction_to_PDF
 * http://www.planetpdf.com/mainpage.asp?WebPageID=63
 * http://archive.vector.org.uk/art10008970
 * http://www.adobe.com/devnet/acrobat/pdfs/pdf_reference_1-7.pdf
 * https://blog.idrsolutions.com/2013/01/understanding-the-pdf-file-format-overview/
 *
 * To validate the PDF output, there are several online validators:
 * http://www.validatepdfa.com/online.htm
 * http://www.datalogics.com/products/callas/callaspdfA-onlinedemo.asp
 * http://www.pdf-tools.com/pdf/validate-pdfa-online.aspx
 *
 * In addition the 'pdftk' server can be used to analyse the output:
 * https://www.pdflabs.com/docs/pdftk-cli-examples/
 *
 * PDF page markup operators:
 * b    closepath, fill,and stroke path.
 * B    fill and stroke path.
 * b*   closepath, eofill,and stroke path.
 * B*   eofill and stroke path.
 * BI   begin image.
 * BMC  begin marked content.
 * BT   begin text object.
 * BX   begin section allowing undefined operators.
 * c    curveto.
 * cm   concat. Concatenates the matrix to the current transform.
 * cs   setcolorspace for fill.
 * CS   setcolorspace for stroke.
 * d    setdash.
 * Do   execute the named XObject.
 * DP   mark a place in the content stream, with a dictionary.
 * EI   end image.
 * EMC  end marked content.
 * ET   end text object.
 * EX   end section that allows undefined operators.
 * f    fill path.
 * f*   eofill Even/odd fill path.
 * g    setgray (fill).
 * G    setgray (stroke).
 * gs   set parameters in the extended graphics state.
 * h    closepath.
 * i    setflat.
 * ID   begin image data.
 * j    setlinejoin.
 * J    setlinecap.
 * k    setcmykcolor (fill).
 * K    setcmykcolor (stroke).
 * l    lineto.
 * m    moveto.
 * M    setmiterlimit.
 * n    end path without fill or stroke.
 * q    save graphics state.
 * Q    restore graphics state.
 * re   rectangle.
 * rg   setrgbcolor (fill).
 * RG   setrgbcolor (stroke).
 * s    closepath and stroke path.
 * S    stroke path.
 * sc   setcolor (fill).
 * SC   setcolor (stroke).
 * sh   shfill (shaded fill).
 * Tc   set character spacing.
 * Td   move text current point.
 * TD   move text current point and set leading.
 * Tf   set font name and size.
 * Tj   show text.
 * TJ   show text, allowing individual character positioning.
 * TL   set leading.
 * Tm   set text matrix.
 * Tr   set text rendering mode.
 * Ts   set super/subscripting text rise.
 * Tw   set word spacing.
 * Tz   set horizontal scaling.
 * T*   move to start of next line.
 * v    curveto.
 * w    setlinewidth.
 * W    clip.
 * y    curveto.
 */
