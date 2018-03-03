#ifndef _TINYFONT_H
#define _TINYFONT_H

struct font {
	int rows, cols;	/* glyph bitmap rows and columns */
	int n;		/* number of font glyphs */
	int *glyphs;	/* glyph unicode character codes */
	char *data;	/* glyph bitmaps */
};

/*
 * This tinyfont header is followed by:
 *
 * glyphs[n]	unicode character codes (int)
 * bitmaps[n]	character bitmaps (char[rows * cols])
 */
struct tinyfont {
	char sig[8];	/* tinyfont signature; "tinyfont" */
	int ver;	/* version; 0 */
	int n;		/* number of glyphs */
	int rows, cols;	/* glyph dimensions */
};


struct font *font_open(char *path);
int font_bitmap(struct font *font, void *dst, int c);
void font_free(struct font *font);
int font_rows(struct font *font);
int font_cols(struct font *font);

#endif
