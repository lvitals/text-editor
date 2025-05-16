#ifndef GLYPH_H_
#define GLYPH_H_

#include <SDL.h>

//Generic map to hold glyph positions. Indexed based on ascii value of character.

#define MAX_GLYPHS 200

typedef struct {
    int x;
    int y;
    int w;
    int h;
} Glyph_Rect;

typedef struct {
    int maxGlyphs;
    Glyph_Rect** glyphs;
    int glyphHeight;
} Glyph_Map;

Glyph_Map* createGlyphMap(void);
void freeGlyphMap(Glyph_Map* glyphMap);
void addGlyph(Glyph_Map* glyphMap, char c, SDL_Rect* glyph);
void setGlyphHeight(Glyph_Map* glyphMap, int height);

#endif
