
// Generate PDF files.
//

#ifndef _CC_LIB_PDF_H
#define _CC_LIB_PDF_H

#include <stdint.h>
#include <stdio.h>
#include <optional>
#include <string>

// Allows for quick generation of simple PDF documents.
// This is useful for producing easily printed output from C code, where
// advanced formatting is not required
//
// Note: All coordinates/sizes are in points (1/72 of an inch).
// All coordinates are based on 0,0 being the bottom left of the page.
// All colours are specified as a packed 32-bit value - see @ref PDF_RGB.
// Text strings are interpreted as UTF-8 encoded, but only a small subset of
// characters beyond 7-bit ascii are supported (see @ref pdf_add_text for
// details).

/*
#include "pdfgen.h"
 ...
struct pdf_info info = {
         .creator = "My software",
         .producer = "My software",
         .title = "My document",
         .author = "My name",
         .subject = "My subject",
         .date = "Today"
         };
struct pdf_doc *pdf = pdf_create(PDF_A4_WIDTH, PDF_A4_HEIGHT, &info);
pdf_set_font(pdf, "Times-Roman");
pdf_append_page(pdf);
pdf_add_text(pdf, NULL, "This is text", 12, 50, 20, PDF_BLACK);
pdf_add_line(pdf, NULL, 50, 24, 150, 24);
pdf_save(pdf, "output.pdf");
pdf_destroy(pdf);
 * @endcode
 */

static constexpr inline float PDF_INCH_TO_POINT(float inch) {
  return inch * 72.0f;
}

static constexpr inline float PDF_MM_TO_POINT(float mm) {
  return mm * 72.0f / 25.4f;
}

struct PDF {

  // Metadata to be inserted into the header of the output PDF.
  // Because these fields must be null-terminated, the maximum
  // length is actually 63.
  struct Info {
    // Software used to create the PDF.
    char creator[64] = {};
    char producer[64] = {};
    // The title of the PDF (typically displayed in the
    // window bar when viewing).
    char title[64] = {};
    char author[64] = {};
    char subject[64] = {};
    // The date the PDF was created.
    char date[64] = {};
  };

public:
  // TODO: use vector
  struct flexarray {
      void ***bins;
      int item_count;
      int bin_count;
  };

  enum ObjType {
    OBJ_none, /* skipped */
    OBJ_info,
    OBJ_stream,
    OBJ_font,
    OBJ_page,
    OBJ_bookmark,
    OBJ_outline,
    OBJ_catalog,
    OBJ_pages,
    OBJ_image,
    OBJ_link,

    OBJ_count,
  };

  struct Object {
    Object() {}
    virtual ~Object() {}

    ObjType type = OBJ_none;
    // PDF output index
    int index = 0;
    // Byte position within the output file
    int offset = 0;
    // Previous and next objects of this same type.
    Object *prev = nullptr;
    Object *next = nullptr;
  };

  struct Bookmark : public Object {
    Object *page;
    char name[64];
    Object *parent;
    flexarray children;
  };

  struct Page : public Object {
    // Dimensions of the page, in points.
    float Width() const { return width; }
    float Height() const { return height; }

    void SetSize(float width, float height);

    float width;
    float height;
    flexarray children;
    flexarray annotations;
  };

  struct Stream : public Object {
    Object *page;
    std::string stream;
  };

  struct InfoObj : public Object {
    Info info;
  };

  struct FontObj : public Object {
    char name[64];
    int font_index;
  };

  struct Link : public Object {
    Object *page; /* Page containing link */
    float llx;               /* Clickable rectangle */
    float lly;
    float urx;
    float ury;
    Object *target_page; /* Target page */
    float target_x;                 /* Target location */
    float target_y;
  };

  struct None : public Object { };

  struct Catalog : public Object { };

  // Supported image file formats.
  enum ImageType {
    IMAGE_PNG,
    IMAGE_JPG,
    IMAGE_PPM,
    IMAGE_BMP,

    IMAGE_UNKNOWN
  };

  // TODO: This is used so that the headers below get the same alignment
  // as the file. But it's not standard.
  #pragma pack(push, 1)


  // As defined by https://www.w3.org/TR/2003/REC-PNG-20031110/#6Colour-values
  enum PNGColorType : uint8_t {
    // Greyscale
    PNG_COLOR_GREYSCALE = 0,
    // Truecolour
    PNG_COLOR_RGB = 2,
    // Indexed-colour
    PNG_COLOR_INDEXED = 3,
    // Greyscale with alpha
    PNG_COLOR_GREYSCALE_A = 4,
    // Truecolour with alpha
    PNG_COLOR_RGBA = 6,

    PNG_COLOR_INVALID = 255
  };

  // Header of a PNG file.
  struct png_header {
    // Dimensions in pixels.
    uint32_t width;
    uint32_t height;
    uint8_t bitDepth;
    PNGColorType colorType;
    uint8_t deflate;
    uint8_t filtering;
    uint8_t interlace;
  };

  // Header of a BMP file.
  // XXX Can probably just delete BMP format.
  struct bmp_header {
    uint32_t bfSize;        //!< size of BMP in bytes
    uint16_t bfReserved1;   //!< ignore!
    uint16_t bfReserved2;   //!< ignore!
    uint32_t bfOffBits;     //!< Offset to BMP data
    uint32_t biSize;        //!< Size of this header (40)
    int32_t biWidth;        //!< Width in pixels
    int32_t biHeight;       //!< Height in pixels
    uint16_t biPlanes;      //!< Number of colour planes - must be 1
    uint16_t biBitCount;    //!< Bits Per Pixel
    uint32_t biCompression; //!< Compression Method
  };
#pragma pack(pop)

  // Header for a JPEG file.
  struct jpeg_header {
    int ncolours;
  };

  enum PPMColorSpace {
    PPM_BINARY_COLOR_RGB,  //!< binary ppm with RGB colors (magic number P5)
    PPM_BINARY_COLOR_GRAY, //!< binary ppm with grayscale colors (magic number
                           //!< P6)
  };

  struct ppm_header {
    size_t size;           //!< Indicate the size of the image data
    size_t data_begin_pos; //!< position in the data where the image starts
    int color_space;       //!< PPM color space
  };

  /**
   * pdf_img_info describes the metadata for an arbitrary image
   */
  struct pdf_img_info {
    int image_format; //!< Indicates the image format (IMAGE_PNG, ...)
    uint32_t width;   //!< Width in pixels
    uint32_t height;  //!< Height in pixels

    //!< Image specific details
    union {
        struct bmp_header bmp;   //!< BMP header info
        struct jpeg_header jpeg; //!< JPEG header info
        struct png_header png;   //!< PNG header info
        struct ppm_header ppm;   //!< PPM header info
    };
  };

  // A drawing operation within a path.
  //  See PDF reference for detailed usage.
  struct pdf_path_operation {
    char op;  /*!< Operation command. Possible operators are: m = move to, l =
                 line to, c = cubic bezier curve with two control points, v =
                 cubic bezier curve with one control point fixed at first
                 point, y = cubic bezier curve with one control point fixed
                 at second point, h = close path */
    float x1; /*!< X offset of the first point. Used with: m, l, c, v, y */
    float y1; /*!< Y offset of the first point. Used with: m, l, c, v, y */
    float x2; /*!< X offset of the second point. Used with: c, v, y */
    float y2; /*!< Y offset of the second point. Used with: c, v, y */
    float x3; /*!< X offset of the third point. Used with: c */
    float y3; /*!< Y offset of the third point. Used with: c */
  };

  // XXX As functions.

  // US Letter page.
  static constexpr float PDF_LETTER_WIDTH = PDF_INCH_TO_POINT(8.5f);
  static constexpr float PDF_LETTER_HEIGHT = PDF_INCH_TO_POINT(11.0f);

  static constexpr float PDF_A4_WIDTH = PDF_MM_TO_POINT(210.0f);
  static constexpr float PDF_A4_HEIGHT = PDF_MM_TO_POINT(297.0f);

  static constexpr float PDF_A3_WIDTH = PDF_MM_TO_POINT(297.0f);
  static constexpr float PDF_A3_HEIGHT = PDF_MM_TO_POINT(420.0f);

  // XXX to functions/constants. Maybe should switch to RGBA.

  // Pack R, G, B bytes into a 32-bit value.
  #define PDF_RGB(r, g, b)                                              \
    (uint32_t)((((r)&0xff) << 16) | (((g)&0xff) << 8) | (((b)&0xff)))

  // Pack A, R, G, B bytes into a 32-bit value in the order ARGB.
  #define PDF_ARGB(a, r, g, b)                                            \
    (uint32_t)(((uint32_t)((a)&0xff) << 24) | (((r)&0xff) << 16) |        \
               (((g)&0xff) << 8) | (((b)&0xff)))

  /*! Utility macro to provide bright red */
  #define PDF_RED PDF_RGB(0xff, 0, 0)

  /*! Utility macro to provide bright green */
  #define PDF_GREEN PDF_RGB(0, 0xff, 0)

  /*! Utility macro to provide bright blue */
  #define PDF_BLUE PDF_RGB(0, 0, 0xff)

  /*! Utility macro to provide black */
  #define PDF_BLACK PDF_RGB(0, 0, 0)

  /*! Utility macro to provide white */
  #define PDF_WHITE PDF_RGB(0xff, 0xff, 0xff)

  /*!
   * Utility macro to provide a transparent colour
   * This is used in some places for 'fill' colours, where no fill is required
   */
  #define PDF_TRANSPARENT (uint32_t)(0xffu << 24)

  // Text alignment.
  enum Alignment {
    PDF_ALIGN_LEFT,
    PDF_ALIGN_RIGHT,
    PDF_ALIGN_CENTER,
    // Align text in the center, with padding to fill the
    // available space.
    PDF_ALIGN_JUSTIFY,
    // Like PDF_ALIGN_JUSTIFY, except even short
    // lines will be fully justified
    PDF_ALIGN_JUSTIFY_ALL,
    // Fake alignment for only checking wrap height with
    // no writes
    PDF_ALIGN_NO_WRITE,
  };

  // Constructor. Give width and height of the page, and optional
  // header info.
  PDF(float width, float height, const std::optional<Info> &info);

  ~PDF();

  // If an operation fails, this gets the error code and a human-readable
  // error message.
  int GetErrCode() const;
  std::string GetErr() const;
  // Acknowledge an outstanding error.
  void ClearErr();

  /**
   * Sets the font to use for text objects. Default value is Times-Roman if
   * this function is not called.
   * Note: The font selection should be done before text is output,
   * and will remain until pdf_set_font is called again.
   * @param pdf PDF document to update font on
   * @param font New font to use. This must be one of the standard PDF fonts:
   *  Courier, Courier-Bold, Courier-BoldOblique, Courier-Oblique,
   *  Helvetica, Helvetica-Bold, Helvetica-BoldOblique, Helvetica-Oblique,
   *  Times-Roman, Times-Bold, Times-Italic, Times-BoldItalic,
   *  Symbol or ZapfDingbats
   * @return < 0 on failure, 0 on success
   */
  int SetFont(const char *font);

  /**
   * Calculate the width of a given string in the current font
   * @param pdf PDF document
   * @param font_name Name of the font to get the width of.
   *  This must be one of the standard PDF fonts:
   *  Courier, Courier-Bold, Courier-BoldOblique, Courier-Oblique,
   *  Helvetica, Helvetica-Bold, Helvetica-BoldOblique, Helvetica-Oblique,
   *  Times-Roman, Times-Bold, Times-Italic, Times-BoldItalic,
   *  Symbol or ZapfDingbats
   * @param text Text to determine width of
   * @param size Size of the text, in points
   * @param text_width area to store calculated width in
   * @return < 0 on failure, 0 on success
   */
  int GetFontTextWidth(const char *font_name,
                       const char *text, float size, float *text_width);

  // Dimensions of the document, in points.
  float Width() const;
  float Height() const;

  // Returns null on failure.
  Page *AppendNewPage();

  // Retrieve a page by its number (starting from 1).
  // Note: The page must have already been created via AppendNewPage.
  // Returns null if the page is not found.
  Page *GetPage(int page_number);

  // Save the given pdf document to the supplied filename.
  // Pass the name of the file to store the PDF into (NULL for stdout)
  // Returns < 0 on failure, >= 0 on success
  // int pdf_save(struct pdf_doc *pdf, const char *filename);
  bool Save(const char *filename);

  #if 0
/**
 * Adjust the width/height of a specific page
 * @param pdf PDF document that the page belongs to
 * @param page object returned from @ref pdf_append_page
 * @param width Width of the page in points
 * @param height Height of the page in points
 * @return < 0 on failure, 0 on success
 */
int pdf_page_set_size(struct pdf_doc *pdf, struct Object *page,
                      float width, float height);

/**
 * Save the given pdf document to the given FILE output
 * @param pdf PDF document to save
 * @param fp FILE pointer to store the data into (must be writable)
 * @return < 0 on failure, >= 0 on success
 */
int pdf_save_file(struct pdf_doc *pdf, FILE *fp);

/**
 * Add a text string to the document
 * @param pdf PDF document to add to
 * @param page Page to add object to (NULL => most recently added page)
 * @param text String to display
 * @param size Point size of the font
 * @param xoff X location to put it in
 * @param yoff Y location to put it in
 * @param colour Colour to draw the text
 * @return 0 on success, < 0 on failure
 */
int pdf_add_text(struct pdf_doc *pdf, struct Object *page,
                 const char *text, float size, float xoff, float yoff,
                 uint32_t colour);

/**
 * Add a text string to the document at a rotated angle
 * @param pdf PDF document to add to
 * @param page Page to add object to (NULL => most recently added page)
 * @param text String to display
 * @param size Point size of the font
 * @param xoff X location to put it in
 * @param yoff Y location to put it in
 * @param angle Rotation angle of text (in radians)
 * @param colour Colour to draw the text
 * @return 0 on success, < 0 on failure
 */
int pdf_add_text_rotate(struct pdf_doc *pdf, struct Object *page,
                        const char *text, float size, float xoff, float yoff,
                        float angle, uint32_t colour);
/**
 * Add a text string to the document, making it wrap if it is too
 * long
 * @param pdf PDF document to add to
 * @param page Page to add object to (NULL => most recently added page)
 * @param text String to display
 * @param size Point size of the font
 * @param xoff X location to put it in
 * @param yoff Y location to put it in
 * @param angle Rotation angle of text (in radians)
 * @param colour Colour to draw the text
 * @param wrap_width Width at which to wrap the text
 * @param align Text alignment (see PDF_ALIGN_xxx)
 * @param height Store the final height of the wrapped text here (optional)
 * @return < 0 on failure, >= 0 on success
 */
int pdf_add_text_wrap(struct pdf_doc *pdf, struct Object *page,
                      const char *text, float size, float xoff, float yoff,
                      float angle, uint32_t colour, float wrap_width,
                      int align, float *height);

/**
 * Add a line to the document
 * @param pdf PDF document to add to
 * @param page Page to add object to (NULL => most recently added page)
 * @param x1 X offset of start of line
 * @param y1 Y offset of start of line
 * @param x2 X offset of end of line
 * @param y2 Y offset of end of line
 * @param width Width of the line
 * @param colour Colour to draw the line
 * @return 0 on success, < 0 on failure
 */
int pdf_add_line(struct pdf_doc *pdf, struct Object *page, float x1,
                 float y1, float x2, float y2, float width, uint32_t colour);

/**
 * Add a cubic bezier curve to the document
 * @param pdf PDF document to add to
 * @param page Page to add object to (NULL => most recently added page)
 * @param x1 X offset of the initial point of the curve
 * @param y1 Y offset of the initial point of the curve
 * @param x2 X offset of the final point of the curve
 * @param y2 Y offset of the final point of the curve
 * @param xq1 X offset of the first control point of the curve
 * @param yq1 Y offset of the first control point of the curve
 * @param xq2 X offset of the second control of the curve
 * @param yq2 Y offset of the second control of the curve
 * @param width Width of the curve
 * @param colour Colour to draw the curve
 * @return 0 on success, < 0 on failure
 */
int pdf_add_cubic_bezier(struct pdf_doc *pdf, struct Object *page,
                         float x1, float y1, float x2, float y2, float xq1,
                         float yq1, float xq2, float yq2, float width,
                         uint32_t colour);

/**
 * Add a quadratic bezier curve to the document
 * @param pdf PDF document to add to
 * @param page Page to add object to (NULL => most recently added page)
 * @param x1 X offset of the initial point of the curve
 * @param y1 Y offset of the initial point of the curve
 * @param x2 X offset of the final point of the curve
 * @param y2 Y offset of the final point of the curve
 * @param xq1 X offset of the control point of the curve
 * @param yq1 Y offset of the control point of the curve
 * @param width Width of the curve
 * @param colour Colour to draw the curve
 * @return 0 on success, < 0 on failure
 */
int pdf_add_quadratic_bezier(struct pdf_doc *pdf, struct Object *page,
                             float x1, float y1, float x2, float y2,
                             float xq1, float yq1, float width,
                             uint32_t colour);

/**
 * Add a custom path to the document
 * @param pdf PDF document to add to
 * @param page Page to add object to (NULL => most recently added page)
 * @param operations Array of drawing operations
 * @param operation_count The number of operations
 * @param stroke_width Width of the stroke
 * @param stroke_colour Colour to stroke the curve
 * @param fill_colour Colour to fill the path
 * @return 0 on success, < 0 on failure
 */
int pdf_add_custom_path(struct pdf_doc *pdf, struct Object *page,
                        const struct pdf_path_operation *operations,
                        int operation_count, float stroke_width,
                        uint32_t stroke_colour, uint32_t fill_colour);

/**
 * Add an ellipse to the document
 * @param pdf PDF document to add to
 * @param page Page to add object to (NULL => most recently added page)
 * @param x X offset of the center of the ellipse
 * @param y Y offset of the center of the ellipse
 * @param xradius Radius of the ellipse in the X axis
 * @param yradius Radius of the ellipse in the Y axis
 * @param width Width of the ellipse outline stroke
 * @param colour Colour to draw the ellipse outline stroke
 * @param fill_colour Colour to fill the ellipse
 * @return 0 on success, < 0 on failure
 */
int pdf_add_ellipse(struct pdf_doc *pdf, struct Object *page, float x,
                    float y, float xradius, float yradius, float width,
                    uint32_t colour, uint32_t fill_colour);

/**
 * Add a circle to the document
 * @param pdf PDF document to add to
 * @param page Page to add object to (NULL => most recently added page)
 * @param x X offset of the center of the circle
 * @param y Y offset of the center of the circle
 * @param radius Radius of the circle
 * @param width Width of the circle outline stroke
 * @param colour Colour to draw the circle outline stroke
 * @param fill_colour Colour to fill the circle
 * @return 0 on success, < 0 on failure
 */
int pdf_add_circle(struct pdf_doc *pdf, struct Object *page, float x,
                   float y, float radius, float width, uint32_t colour,
                   uint32_t fill_colour);

/**
 * Add an outline rectangle to the document
 * @param pdf PDF document to add to
 * @param page Page to add object to (NULL => most recently added page)
 * @param x X offset to start rectangle at
 * @param y Y offset to start rectangle at
 * @param width Width of rectangle
 * @param height Height of rectangle
 * @param border_width Width of rectangle border
 * @param colour Colour to draw the rectangle
 * @return 0 on success, < 0 on failure
 */
int pdf_add_rectangle(struct pdf_doc *pdf, struct Object *page, float x,
                      float y, float width, float height, float border_width,
                      uint32_t colour);

/**
 * Add a filled rectangle to the document
 * @param pdf PDF document to add to
 * @param page Page to add object to (NULL => most recently added page)
 * @param x X offset to start rectangle at
 * @param y Y offset to start rectangle at
 * @param width Width of rectangle
 * @param height Height of rectangle
 * @param border_width Width of rectangle border
 * @param colour_fill Colour to fill the rectangle
 * @param colour_border Colour to draw the rectangle
 * @return 0 on success, < 0 on failure
 */
int pdf_add_filled_rectangle(struct pdf_doc *pdf, struct Object *page,
                             float x, float y, float width, float height,
                             float border_width, uint32_t colour_fill,
                             uint32_t colour_border);

/**
 * Add an outline polygon to the document
 * @param pdf PDF document to add to
 * @param page Page to add object to (NULL => most recently added page)
 * @param x array of X offsets for points comprising the polygon
 * @param y array of Y offsets for points comprising the polygon
 * @param count Number of points comprising the polygon
 * @param border_width Width of polygon border
 * @param colour Colour to draw the polygon
 * @return 0 on success, < 0 on failure
 */
int pdf_add_polygon(struct pdf_doc *pdf, struct Object *page, float x[],
                    float y[], int count, float border_width,
                    uint32_t colour);

/**
 * Add a filled polygon to the document
 * @param pdf PDF document to add to
 * @param page Page to add object to (NULL => most recently added page)
 * @param x array of X offsets of points comprising the polygon
 * @param y array of Y offsets of points comprising the polygon
 * @param count Number of points comprising the polygon
 * @param border_width Width of polygon border
 * @param colour Colour to draw the polygon
 * @return 0 on success, < 0 on failure
 */
int pdf_add_filled_polygon(struct pdf_doc *pdf, struct Object *page,
                           float x[], float y[], int count,
                           float border_width, uint32_t colour);

/**
 * Add a bookmark to the document
 * @param pdf PDF document to add bookmark to
 * @param page Page to jump to for bookmark
               (or NULL for the most recently added page)
 * @param parent ID of a previously created bookmark that is the parent
               of this one. -1 if this should be a top-level bookmark.
 * @param name String to associate with the bookmark
 * @return < 0 on failure, new bookmark id on success
 */
int pdf_add_bookmark(struct pdf_doc *pdf, struct Object *page, int parent,
                     const char *name);

/**
 * Add a link annotation to the document
 * @param pdf PDF document to add link to
 * @param page Page that holds the clickable rectangle
               (or NULL for the most recently added page)
 * @param x X coordinate of bottom LHS corner of clickable rectangle
 * @param y Y coordinate of bottom LHS corner of clickable rectangle
 * @param width width of clickable rectangle
 * @param height height of clickable rectangle
 * @param target_page Page to jump to for link
 * @param target_x X coordinate to position at the left of the view
 * @param target_y Y coordinate to position at the top of the view
 * @return < 0 on failure, new bookmark id on success
 */
int pdf_add_link(struct pdf_doc *pdf, struct Object *page, float x,
                 float y, float width, float height,
                 struct Object *target_page, float target_x,
                 float target_y);

/**
 * List of different barcode encodings that are supported
 */
enum {
    PDF_BARCODE_128A,  //!< Produce code-128A style barcodes
    PDF_BARCODE_39,    //!< Produce code-39 style barcodes
    PDF_BARCODE_EAN13, //!< Produce EAN-13 style barcodes
    PDF_BARCODE_UPCA,  //!< Produce UPC-A style barcodes
    PDF_BARCODE_EAN8,  //!< Produce EAN-8 style barcodes
    PDF_BARCODE_UPCE,  //!< Produce UPC-E style barcodes
};

/**
 * Add a barcode to the document
 * @param pdf PDF document to add barcode to
 * @param page Page to add barcode to (NULL => most recently added page)
 * @param code Type of barcode to add (PDF_BARCODE_xxx)
 * @param x X offset to put barcode at
 * @param y Y offset to put barcode at
 * @param width Width of barcode
 * @param height Height of barcode
 * @param string Barcode contents
 * @param colour Colour to draw barcode
 * @return < 0 on failure, >= 0 on success
 */
int pdf_add_barcode(struct pdf_doc *pdf, struct Object *page, int code,
                    float x, float y, float width, float height,
                    const char *string, uint32_t colour);

/**
 * Add image data as an image to the document.
 * Image data must be one of: JPEG, PNG, PPM, PGM or BMP formats
 * Passing 0 for either the display width or height will
 * include the image but not render it visible.
 * Passing a negative number either the display height or width will
 * have the image be resized while keeping the original aspect ratio.
 * @param pdf PDF document to add image to
 * @param page Page to add image to (NULL => most recently added page)
 * @param x X offset to put image at
 * @param y Y offset to put image at
 * @param display_width Displayed width of image
 * @param display_height Displayed height of image
 * @param data Image data bytes
 * @param len Length of data
 * @return < 0 on failure, >= 0 on success
 */
int pdf_add_image_data(struct pdf_doc *pdf, struct Object *page, float x,
                       float y, float display_width, float display_height,
                       const uint8_t *data, size_t len);

/**
 * Add a raw 24 bit per pixel RGB buffer as an image to the document
 * Passing 0 for either the display width or height will
 * include the image but not render it visible.
 * Passing a negative number either the display height or width will
 * have the image be resized while keeping the original aspect ratio.
 * @param pdf PDF document to add image to
 * @param page Page to add image to (NULL => most recently added page)
 * @param x X offset to put image at
 * @param y Y offset to put image at
 * @param display_width Displayed width of image
 * @param display_height Displayed height of image
 * @param data RGB data to add
 * @param width width of image in pixels
 * @param height height of image in pixels
 * @return < 0 on failure, >= 0 on success
 */
int pdf_add_rgb24(struct pdf_doc *pdf, struct Object *page, float x,
                  float y, float display_width, float display_height,
                  const uint8_t *data, uint32_t width, uint32_t height);

/**
 * Add a raw 8 bit per pixel grayscale buffer as an image to the document
 * @param pdf PDF document to add image to
 * @param page Page to add image to (NULL => most recently added page)
 * @param x X offset to put image at
 * @param y Y offset to put image at
 * @param display_width Displayed width of image
 * @param display_height Displayed height of image
 * @param data grayscale pixel data to add
 * @param width width of image in pixels
 * @param height height of image in pixels
 * @return < 0 on failure, >= 0 on success
 */
int pdf_add_grayscale8(struct pdf_doc *pdf, struct Object *page, float x,
                       float y, float display_width, float display_height,
                       const uint8_t *data, uint32_t width, uint32_t height);

/**
 * Add an image file as an image to the document.
 * Passing 0 for either the display width or height will
 * include the image but not render it visible.
 * Passing a negative number either the display height or width will
 * have the image be resized while keeping the original aspect ratio.
 * Supports image formats: JPEG, PNG, PPM, PGM & BMP
 * @param pdf PDF document to add bookmark to
 * @param page Page to add image to (NULL => most recently added page)
 * @param x X offset to put image at
 * @param y Y offset to put image at
 * @param display_width Displayed width of image
 * @param display_height Displayed height of image
 * @param image_filename Filename of image file to display
 * @return < 0 on failure, >= 0 on success
 */
int pdf_add_image_file(struct pdf_doc *pdf, struct Object *page, float x,
                       float y, float display_width, float display_height,
                       const char *image_filename);

/**
 * Parse image data to determine the image type & metadata
 * @param info structure to hold the parsed metadata
 * @param data image data to parse
 * @param length number of bytes in data
 * @param err_msg area to put any failure details
 * @param err_msg_length maximum number of bytes to store in err_msg
 * @return < 0 on failure, >= 0 on success
 */
int pdf_parse_image_header(struct pdf_img_info *info, const uint8_t *data,
                           size_t length, char *err_msg,
                           size_t err_msg_length);
#endif

private:

  int SetErr(int errval, const char *buffer, ...);
  Object *pdf_get_object(int index);
  bool pdf_append_object(Object *obj);
  // Perhaps can just be destructor, or static
  void pdf_object_destroy(Object *object);
  // Add a new-ly allocated object; takes ownership.
  Object *pdf_add_object_internal(Object *obj);
  // Convenience method that casts back to the derived class.
  template<class T> inline T *AddObject(T *t) {
    return (T*)pdf_add_object_internal(t);
  }
  void pdf_del_object(Object *obj);
  Object *pdf_find_first_object(int type);
  Object *pdf_find_last_object(int type);
  static int pdf_get_bookmark_count(const Object *obj);
  int pdf_add_stream(Page *page, const char *buffer);
  int pdf_save_file(FILE *fp);
  int pdf_save_object(FILE *fp, int index);

  char errstr[128] = {};
  int errval = 0;
  struct flexarray objects;

  float width = 0.0f;
  float height = 0.0f;

  FontObj *current_font = nullptr;

  Object *last_objects[OBJ_count] = {};
  Object *first_objects[OBJ_count] = {};
};

#endif

