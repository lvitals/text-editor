#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <SDL.h>
#include <SDL_ttf.h>
#include "vec.h"
#include "glyph.h"
#include "gap.h"
#include "line.h"
#include "file.h"

#define MAX_BUFFER_SIZE 1024
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

typedef struct
{
    size_t line;
    size_t index;
    Vec2 pos;
    int preferred_x;
} Cursor;

typedef struct
{
    size_t start_line;
    size_t start_index;
    size_t end_line;
    size_t end_index;
} Selection;

typedef struct
{
    int x;
    int y;
    int max_x;
    int max_y;
    int win_w;
    int win_h;
} ScrollState;

void sdl_cc(int code)
{
    if (code < 0)
    {
        fprintf(stderr, "SDL ERROR: %s\n", SDL_GetError());
        exit(1);
    }
}

void *sdl_cp(void *ptr)
{
    if (ptr == NULL)
    {
        fprintf(stderr, "SDL ERROR: %s\n", SDL_GetError());
        exit(1);
    }
    return ptr;
}

void loadFont(const char *fontFile, int fontSize, TTF_Font **font_ptr)
{
    if (font_ptr != NULL && *font_ptr != NULL)
    {
        TTF_CloseFont(*font_ptr);
    }
    *font_ptr = sdl_cp(TTF_OpenFont(fontFile, fontSize));
}

void copyRect_GS(Glyph_Rect *srcRect, SDL_Rect *dstRect)
{
    dstRect->x = srcRect->x;
    dstRect->y = srcRect->y;
    dstRect->w = srcRect->w;
    dstRect->h = srcRect->h;
}

SDL_Texture *cacheTexture(SDL_Renderer *renderer, TTF_Font *font, Glyph_Map *glyphMap)
{
    SDL_Color color = {255, 255, 255, 255};
    int height = TTF_FontHeight(font);
    int ascent = TTF_FontAscent(font);
    int descent = TTF_FontDescent(font);

    if (ascent - descent != height)
    {
        height = ascent - descent;
    }

    int maxWidth = height * 12;
    int maxHeight = height * 12;
    glyphMap->glyphHeight = height;

    SDL_Surface *cacheSurface = sdl_cp(SDL_CreateRGBSurface(SDL_SWSURFACE, maxWidth, maxHeight, 32,
                                                            0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000));

    char asciiString[95] = "";
    for (int i = 0; i < 95; i++)
    {
        asciiString[i] = (char)(32 + i);
    }

    SDL_Rect cacheCursor = {.x = 0, .y = 0, .w = 0, .h = height};
    for (size_t i = 0; i < strlen(asciiString); i++)
    {
        SDL_Surface *glyphSurface = sdl_cp(TTF_RenderGlyph_Blended(font, asciiString[i], color));
        sdl_cc(SDL_SetSurfaceBlendMode(glyphSurface, SDL_BLENDMODE_NONE));

        if (cacheCursor.x + glyphSurface->w >= maxWidth)
        {
            cacheCursor.x = 0;
            cacheCursor.y += height;
        }

        cacheCursor.w = glyphSurface->w;
        cacheCursor.h = glyphSurface->h;
        SDL_Rect srcRect = {0, 0, glyphSurface->w, glyphSurface->h};
        SDL_Rect dstRect = {cacheCursor.x, cacheCursor.y, cacheCursor.w, cacheCursor.h};
        sdl_cc(SDL_BlitSurface(glyphSurface, &srcRect, cacheSurface, &dstRect));
        addGlyph(glyphMap, asciiString[i], &cacheCursor);
        cacheCursor.x += glyphSurface->w;
        SDL_FreeSurface(glyphSurface);
    }

    SDL_Texture *cacheTexture = sdl_cp(SDL_CreateTextureFromSurface(renderer, cacheSurface));
    SDL_FreeSurface(cacheSurface);
    return cacheTexture;
}

int calculateCursorX(GapBuffer *line, Glyph_Map *glyphMap, size_t cursor_pos)
{
    int x = 0;
    for (size_t i = 0; i < cursor_pos && i < line->cursor; i++)
    {
        int glyph = line->string[i];
        if (glyph >= 32 && glyph <= 126)
        {
            x += glyphMap->glyphs[glyph - 32]->w;
        }
    }
    for (size_t i = line->gapEnd; i < line->length && i < line->gapEnd + (cursor_pos - line->cursor); i++)
    {
        int glyph = line->string[i];
        if (glyph >= 32 && glyph <= 126)
        {
            x += glyphMap->glyphs[glyph - 32]->w;
        }
    }
    return x;
}

size_t findCursorPosition(GapBuffer *line, Glyph_Map *glyphMap, int target_x)
{
    int current_x = 0;
    size_t pos = 0;

    // Check characters before gap
    for (; pos < line->cursor; pos++)
    {
        int glyph = line->string[pos];
        if (glyph >= 32 && glyph <= 126)
        {
            int char_width = glyphMap->glyphs[glyph - 32]->w;
            if (current_x + char_width / 2 > target_x)
            {
                return pos;
            }
            current_x += char_width;
        }
    }

    // Check characters after gap
    for (size_t i = line->gapEnd; i < line->length; i++)
    {
        int glyph = line->string[i];
        if (glyph >= 32 && glyph <= 126)
        {
            int char_width = glyphMap->glyphs[glyph - 32]->w;
            if (current_x + char_width / 2 > target_x)
            {
                return pos;
            }
            current_x += char_width;
            pos++;
        }
    }

    return pos;
}

void renderCursor(SDL_Renderer *renderer, Cursor *cursor, GapBuffer *text,
                  SDL_Texture *cursorTexture, Glyph_Map *glyphMap, ScrollState *scroll)
{
    SDL_Rect destRect = {
        .x = -scroll->x,
        .y = (int)(cursor->line - scroll->y) * glyphMap->glyphHeight,
        .w = glyphMap->glyphHeight / 2,
        .h = glyphMap->glyphHeight};

    destRect.x += calculateCursorX(text, glyphMap, cursor->index);
    sdl_cc(SDL_RenderCopy(renderer, cursorTexture, NULL, &destRect));
}

void renderSelection(SDL_Renderer *renderer, Selection *selection, Text *text,
                     Glyph_Map *glyphMap, ScrollState *scroll)
{
    if (selection->start_line == selection->end_line &&
        selection->start_index == selection->end_index)
    {
        return;
    }

    // Determine the actual start and end of selection
    size_t sel_start_line, sel_start_index, sel_end_line, sel_end_index;
    if (selection->start_line < selection->end_line ||
        (selection->start_line == selection->end_line &&
         selection->start_index <= selection->end_index))
    {
        sel_start_line = selection->start_line;
        sel_start_index = selection->start_index;
        sel_end_line = selection->end_line;
        sel_end_index = selection->end_index;
    }
    else
    {
        sel_start_line = selection->end_line;
        sel_start_index = selection->end_index;
        sel_end_line = selection->start_line;
        sel_end_index = selection->start_index;
    }

    // Set selection color with transparency
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 100, 150, 255, 100); // Azul claro semi-transparente

    // Render selection for each line
    for (size_t line = sel_start_line; line <= sel_end_line; line++)
    {
        if (line >= text->lineCount)
            break;

        GapBuffer *current_line = text->lines[line];
        int line_y = (int)(line - scroll->y) * glyphMap->glyphHeight;

        size_t start_idx = (line == sel_start_line) ? sel_start_index : 0;
        size_t end_idx = (line == sel_end_line) ? sel_end_index : gapUsed(current_line);

        if (start_idx >= end_idx)
            continue;

        int start_x = calculateCursorX(current_line, glyphMap, start_idx) - scroll->x;
        int end_x = calculateCursorX(current_line, glyphMap, end_idx) - scroll->x;

        SDL_Rect selection_rect = {
            .x = start_x,
            .y = line_y,
            .w = end_x - start_x,
            .h = glyphMap->glyphHeight};

        SDL_RenderFillRect(renderer, &selection_rect);
    }

    // Reset blend mode
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

void renderChar(SDL_Renderer *renderer, const char c, Vec2 *pos,
                SDL_Texture *font, SDL_Color color, Glyph_Map *glyphMap)
{
    (void)color;

    size_t index = (int)c - 32;
    if (c < 32)
        return;
    if (index >= 95)
        index = 94;

    SDL_Rect fontRect = {0};
    copyRect_GS(glyphMap->glyphs[index], &fontRect);
    SDL_Rect destRect = {pos->x, pos->y, fontRect.w, fontRect.h};
    sdl_cc(SDL_RenderCopy(renderer, font, &fontRect, &destRect));
    pos->x += fontRect.w;
}

void renderLine(SDL_Renderer *renderer, Vec2 *linePos, GapBuffer *line,
                SDL_Texture *font, SDL_Color color, Glyph_Map *glyphMap)
{
    for (size_t i = 0; i < line->cursor; i++)
    {
        renderChar(renderer, line->string[i], linePos, font, color, glyphMap);
    }
    for (size_t i = line->gapEnd; i < line->length; i++)
    {
        renderChar(renderer, line->string[i], linePos, font, color, glyphMap);
    }
}

void renderText(SDL_Renderer *renderer, Text *text, Cursor *cursor, Selection *selection,
                SDL_Texture *fontTexture, SDL_Texture *cursorTexture,
                SDL_Color color, Glyph_Map *glyphMap, ScrollState *scroll)
{
    Vec2 pen = {.x = -scroll->x, .y = 0};
    int lines_visible = scroll->win_h / glyphMap->glyphHeight;
    int first_line = scroll->y;
    int last_line = MIN((int)text->lineCount, first_line + lines_visible + 1);

    // Calculate max line width for horizontal scrolling
    scroll->max_x = 0;
    for (int i = first_line; i < last_line; i++)
    {
        int line_width = 0;
        GapBuffer *line = text->lines[i];

        for (size_t j = 0; j < line->cursor; j++)
        {
            int glyph = line->string[j];
            if (glyph >= 32 && glyph <= 126)
            {
                line_width += glyphMap->glyphs[glyph - 32]->w;
            }
        }

        for (size_t j = line->gapEnd; j < line->length; j++)
        {
            int glyph = line->string[j];
            if (glyph >= 32 && glyph <= 126)
            {
                line_width += glyphMap->glyphs[glyph - 32]->w;
            }
        }

        if (line_width > scroll->max_x)
        {
            scroll->max_x = line_width;
        }
    }

    scroll->max_x = MAX(0, scroll->max_x - scroll->win_w);

    // Render visible lines
    for (int i = first_line; i < last_line; i++)
    {
        pen.y = (i - first_line) * glyphMap->glyphHeight;
        renderLine(renderer, &pen, text->lines[i], fontTexture, color, glyphMap);
        pen.x = -scroll->x;
    }

    // Render selection
    renderSelection(renderer, selection, text, glyphMap, scroll);

    // Render cursor if visible
    if (cursor->line >= (size_t)first_line && cursor->line < (size_t)last_line)
    {
        renderCursor(renderer, cursor, text->lines[cursor->line], cursorTexture, glyphMap, scroll);
    }
}

void updateScrollMax(ScrollState *scroll, Text *text, Glyph_Map *glyphMap)
{
    int lines_visible = scroll->win_h / glyphMap->glyphHeight;
    scroll->max_y = MAX(0, (int)text->lineCount - lines_visible);
    scroll->y = MIN(scroll->y, scroll->max_y);
}

void copySelectedText(Text *text, Selection *selection, char *clipboard, size_t clipboard_size)
{
    if (selection->start_line == selection->end_line &&
        selection->start_index == selection->end_index)
    {
        clipboard[0] = '\0';
        return;
    }

    // Determine the actual start and end of selection
    size_t sel_start_line, sel_start_index, sel_end_line, sel_end_index;
    if (selection->start_line < selection->end_line ||
        (selection->start_line == selection->end_line &&
         selection->start_index <= selection->end_index))
    {
        sel_start_line = selection->start_line;
        sel_start_index = selection->start_index;
        sel_end_line = selection->end_line;
        sel_end_index = selection->end_index;
    }
    else
    {
        sel_start_line = selection->end_line;
        sel_start_index = selection->end_index;
        sel_end_line = selection->start_line;
        sel_end_index = selection->start_index;
    }

    size_t clipboard_pos = 0;
    for (size_t line = sel_start_line; line <= sel_end_line && clipboard_pos < clipboard_size - 1; line++)
    {
        if (line >= text->lineCount)
            break;

        GapBuffer *current_line = text->lines[line];
        size_t start_idx = (line == sel_start_line) ? sel_start_index : 0;
        size_t end_idx = (line == sel_end_line) ? sel_end_index : gapUsed(current_line);

        if (start_idx >= end_idx)
            continue;

        // Copy characters before gap
        for (size_t i = start_idx; i < MIN(end_idx, current_line->cursor) && clipboard_pos < clipboard_size - 1; i++)
        {
            clipboard[clipboard_pos++] = current_line->string[i];
        }

        // Copy characters after gap
        for (size_t i = current_line->gapEnd;
             i < current_line->length &&
             i < current_line->gapEnd + (end_idx - current_line->cursor) &&
             clipboard_pos < clipboard_size - 1;
             i++)
        {
            clipboard[clipboard_pos++] = current_line->string[i];
        }

        // Add newline if not the last line
        if (line != sel_end_line && clipboard_pos < clipboard_size - 1)
        {
            clipboard[clipboard_pos++] = '\n';
        }
    }
    clipboard[clipboard_pos] = '\0';
}

void pasteText(Text *text, Cursor *cursor, Selection *selection, const char *clipboard)
{
    // Delete selected text if any
    if (selection->start_line != selection->end_line || selection->start_index != selection->end_index)
    {
        // TODO: Implement deletion of selected text
        selection->start_line = selection->end_line = cursor->line;
        selection->start_index = selection->end_index = cursor->index;
    }

    // Insert clipboard content
    const char *ptr = clipboard;
    while (*ptr != '\0')
    {
        if (*ptr == '\n')
        {
            cursor->line++;
            createNewLine(text, cursor->line, cursor->index);
            cursor->index = 0;
        }
        else
        {
            char ch[2] = {*ptr, '\0'};
            insertOnLine(text, cursor->line, ch, 1);
            cursor->index++;
        }
        ptr++;
    }
}

void selectAll(Text *text, Selection *selection)
{
    selection->start_line = 0;
    selection->start_index = 0;
    selection->end_line = text->lineCount - 1;
    selection->end_index = gapUsed(text->lines[text->lineCount - 1]);
}

int main(int argc, char const *argv[])
{
    sdl_cc(SDL_Init(SDL_INIT_VIDEO));
    sdl_cc(TTF_Init());

    TTF_Font *font = NULL;
    loadFont("DejaVuSansMono.ttf", 24, &font);
    SDL_Window *window = sdl_cp(SDL_CreateWindow("Text Editor", SDL_WINDOWPOS_CENTERED,
                                                 SDL_WINDOWPOS_CENTERED, 800, 600, SDL_WINDOW_RESIZABLE));
    SDL_Renderer *renderer = sdl_cp(SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED));

    SDL_Color color = {255, 255, 255, 255};
    Glyph_Map *glyphMap = createGlyphMap();
    SDL_Texture *fontTexture = cacheTexture(renderer, font, glyphMap);

    SDL_Surface *cursorSurface = sdl_cp(SDL_CreateRGBSurface(SDL_SWSURFACE, 8, 8, 32,
                                                             0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000));
    sdl_cc(SDL_FillRect(cursorSurface, NULL, 0xAAFFFFFF));
    SDL_Texture *cursorTexture = sdl_cp(SDL_CreateTextureFromSurface(renderer, cursorSurface));
    SDL_FreeSurface(cursorSurface);

    Cursor cursor = {0};
    cursor.preferred_x = 0;
    Selection selection = {0};
    Text *text = createText();
    ScrollState scroll = {0};
    SDL_GetWindowSize(window, &scroll.win_w, &scroll.win_h);

    if (argc >= 2)
    {
        openFile(argv[1], text);
        moveCursor(text->lines[0], 0);
        updateScrollMax(&scroll, text, glyphMap);
    }

    char clipboard[MAX_BUFFER_SIZE] = {0};
    bool mouse_dragging = false;
    bool exit = false;
    bool shift_pressed = false;

    while (!exit)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
            case SDL_QUIT:
                exit = true;
                break;

            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                {
                    scroll.win_w = event.window.data1;
                    scroll.win_h = event.window.data2;
                    updateScrollMax(&scroll, text, glyphMap);
                }
                break;

            case SDL_MOUSEWHEEL:
                if (event.wheel.y > 0)
                {
                    scroll.y = MAX(0, scroll.y - 3);
                }
                else if (event.wheel.y < 0)
                {
                    scroll.y = MIN(scroll.max_y, scroll.y + 3);
                }
                if (event.wheel.x > 0)
                {
                    scroll.x = MIN(scroll.max_x, scroll.x + 20);
                }
                else if (event.wheel.x < 0)
                {
                    scroll.x = MAX(0, scroll.x - 20);
                }
                break;

            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT)
                {
                    int mouse_y = event.button.y;
                    int clicked_line = scroll.y + mouse_y / glyphMap->glyphHeight;

                    if (clicked_line >= 0 && clicked_line < (int)text->lineCount)
                    {
                        cursor.line = clicked_line;
                        int mouse_x = event.button.x + scroll.x;
                        cursor.index = findCursorPosition(text->lines[cursor.line], glyphMap, mouse_x);
                        cursor.preferred_x = calculateCursorX(text->lines[cursor.line], glyphMap, cursor.index);

                        // Start selection
                        selection.start_line = cursor.line;
                        selection.start_index = cursor.index;
                        selection.end_line = cursor.line;
                        selection.end_index = cursor.index;
                        mouse_dragging = true;

                        // Adjust scroll to keep cursor visible
                        int lines_visible = scroll.win_h / glyphMap->glyphHeight;
                        if ((int)cursor.line < scroll.y)
                        {
                            scroll.y = cursor.line;
                        }
                        else if ((int)cursor.line >= scroll.y + lines_visible)
                        {
                            scroll.y = cursor.line - lines_visible + 1;
                        }

                        int cursor_x = calculateCursorX(text->lines[cursor.line], glyphMap, cursor.index);
                        if (cursor_x < scroll.x)
                        {
                            scroll.x = MAX(0, cursor_x - 20);
                        }
                        else if (cursor_x > scroll.x + scroll.win_w - 20)
                        {
                            scroll.x = MIN(scroll.max_x, cursor_x - scroll.win_w + 20);
                        }
                    }
                }
                break;

            case SDL_MOUSEMOTION:
                if (mouse_dragging)
                {
                    int mouse_y = event.motion.y;
                    int clicked_line = scroll.y + mouse_y / glyphMap->glyphHeight;

                    if (clicked_line >= 0 && clicked_line < (int)text->lineCount)
                    {
                        cursor.line = clicked_line;
                        int mouse_x = event.motion.x + scroll.x;
                        cursor.index = findCursorPosition(text->lines[cursor.line], glyphMap, mouse_x);
                        cursor.preferred_x = calculateCursorX(text->lines[cursor.line], glyphMap, cursor.index);

                        // Update selection end
                        selection.end_line = cursor.line;
                        selection.end_index = cursor.index;

                        // Adjust scroll to keep cursor visible
                        int lines_visible = scroll.win_h / glyphMap->glyphHeight;
                        if ((int)cursor.line < scroll.y)
                        {
                            scroll.y = cursor.line;
                        }
                        else if ((int)cursor.line >= scroll.y + lines_visible)
                        {
                            scroll.y = cursor.line - lines_visible + 1;
                        }

                        int cursor_x = calculateCursorX(text->lines[cursor.line], glyphMap, cursor.index);
                        if (cursor_x < scroll.x)
                        {
                            scroll.x = MAX(0, cursor_x - 20);
                        }
                        else if (cursor_x > scroll.x + scroll.win_w - 20)
                        {
                            scroll.x = MIN(scroll.max_x, cursor_x - scroll.win_w + 20);
                        }
                    }
                }
                break;

            case SDL_MOUSEBUTTONUP:
                if (event.button.button == SDL_BUTTON_LEFT)
                {
                    mouse_dragging = false;
                }
                break;

            case SDL_TEXTINPUT:
                if (!(SDL_GetModState() & KMOD_CTRL))
                {
                    // Delete selected text if any
                    if (selection.start_line != selection.end_line || selection.start_index != selection.end_index)
                    {
                        // TODO: Implement deletion of selected text
                        selection.start_line = selection.end_line = cursor.line;
                        selection.start_index = selection.end_index = cursor.index;
                    }

                    size_t textSize = strlen(event.text.text);
                    insertOnLine(text, cursor.line, event.text.text, textSize);
                    cursor.index += textSize;
                    cursor.preferred_x = calculateCursorX(text->lines[cursor.line], glyphMap, cursor.index);
                    updateScrollMax(&scroll, text, glyphMap);
                }
                break;

            case SDL_KEYDOWN:
                switch (event.key.keysym.sym)
                {
                case SDLK_LSHIFT:
                case SDLK_RSHIFT:
                    shift_pressed = true;
                    break;
                    
                case SDLK_EQUALS: // Ctrl + "+"
                    if ((SDL_GetModState() & KMOD_CTRL) && (SDL_GetModState() & KMOD_SHIFT))
                    {
                        int newSize = glyphMap->glyphHeight + 2;
                        if (newSize <= 40)
                        {
                            loadFont("DejaVuSansMono.ttf", newSize, &font);
                            SDL_DestroyTexture(fontTexture);
                            fontTexture = cacheTexture(renderer, font, glyphMap);
                            glyphMap->glyphHeight = newSize;
                            updateScrollMax(&scroll, text, glyphMap);
                        }
                    }
                    break;

                case SDLK_MINUS: // Ctrl + "-"
                    if (SDL_GetModState() & KMOD_CTRL)
                    {
                        int newSize = glyphMap->glyphHeight - 2;
                        if (newSize >= 8)
                        {
                            loadFont("DejaVuSansMono.ttf", newSize, &font);
                            SDL_DestroyTexture(fontTexture);
                            fontTexture = cacheTexture(renderer, font, glyphMap);
                            glyphMap->glyphHeight = newSize;
                            updateScrollMax(&scroll, text, glyphMap);
                        }
                    }
                    break;

                case SDLK_a: // Ctrl+A
                    if (SDL_GetModState() & KMOD_CTRL)
                    {
                        selectAll(text, &selection);
                        cursor.line = selection.end_line;
                        cursor.index = selection.end_index;
                        cursor.preferred_x = calculateCursorX(text->lines[cursor.line], glyphMap, cursor.index);
                    }
                    break;

                case SDLK_c: // Ctrl+C
                    if (SDL_GetModState() & KMOD_CTRL)
                    {
                        copySelectedText(text, &selection, clipboard, MAX_BUFFER_SIZE);
                    }
                    break;

                case SDLK_v: // Ctrl+V
                    if (SDL_GetModState() & KMOD_CTRL)
                    {
                        pasteText(text, &cursor, &selection, clipboard);
                        updateScrollMax(&scroll, text, glyphMap);
                    }
                    break;

                case SDLK_s: // Ctrl+S
                    if ((SDL_GetModState() & KMOD_CTRL) && argc >= 2)
                    {
                        saveFile(argv[1], text);
                    }
                    break;

                case SDLK_BACKSPACE:
                    if (selection.start_line != selection.end_line || selection.start_index != selection.end_index)
                    {
                        // TODO: Implement deletion of selected text
                        selection.start_line = selection.end_line = cursor.line;
                        selection.start_index = selection.end_index = cursor.index;
                    }
                    else if (cursor.index > 0)
                    {
                        cursor.index--;
                        deleteFromLine(text, cursor.line);
                        cursor.preferred_x = calculateCursorX(text->lines[cursor.line], glyphMap, cursor.index);
                    }
                    else if (cursor.line > 0)
                    {
                        cursor.index = deleteLine(text, cursor.line, cursor.index);
                        cursor.line--;
                        cursor.preferred_x = calculateCursorX(text->lines[cursor.line], glyphMap, cursor.index);
                    }
                    updateScrollMax(&scroll, text, glyphMap);
                    break;

                case SDLK_RETURN:
                    if (selection.start_line != selection.end_line || selection.start_index != selection.end_index)
                    {
                        // TODO: Implement deletion of selected text
                        selection.start_line = selection.end_line = cursor.line;
                        selection.start_index = selection.end_index = cursor.index;
                    }
                    cursor.line++;
                    createNewLine(text, cursor.line, cursor.index);
                    cursor.index = 0;
                    cursor.preferred_x = 0;
                    updateScrollMax(&scroll, text, glyphMap);
                    break;

                     case SDLK_LEFT:
                            if (shift_pressed) {
                                // Se Shift está pressionado, estende a seleção
                                if (selection.start_line == selection.end_line && 
                                    selection.start_index == selection.end_index) {
                                    // Se não há seleção, começa uma nova
                                    selection.start_line = cursor.line;
                                    selection.start_index = cursor.index;
                                }
                                
                                if (cursor.index > 0) {
                                    cursorLeft(text->lines[cursor.line]);
                                    cursor.index--;
                                } else if (cursor.line > 0) {
                                    cursor.line--;
                                    cursor.index = moveCursorToEnd(text->lines[cursor.line]);
                                }
                                
                                // Atualiza apenas o final da seleção
                                selection.end_line = cursor.line;
                                selection.end_index = cursor.index;
                                cursor.preferred_x = calculateCursorX(text->lines[cursor.line], glyphMap, cursor.index);
                            } else {
                                // Comportamento normal sem Shift
                                if (selection.start_line != selection.end_line || selection.start_index != selection.end_index) {
                                    cursor.line = selection.start_line;
                                    cursor.index = selection.start_index;
                                } else if (cursor.index > 0) {
                                    cursorLeft(text->lines[cursor.line]);
                                    cursor.index--;
                                } else if (cursor.line > 0) {
                                    cursor.line--;
                                    cursor.index = moveCursorToEnd(text->lines[cursor.line]);
                                }
                                selection.start_line = selection.end_line = cursor.line;
                                selection.start_index = selection.end_index = cursor.index;
                                cursor.preferred_x = calculateCursorX(text->lines[cursor.line], glyphMap, cursor.index);
                            }
                            break;

                        case SDLK_RIGHT:
                            if (shift_pressed) {
                                if (selection.start_line == selection.end_line && 
                                    selection.start_index == selection.end_index) {
                                    selection.start_line = cursor.line;
                                    selection.start_index = cursor.index;
                                }
                                
                                if (cursor.index < gapUsed(text->lines[cursor.line])) {
                                    cursorRight(text->lines[cursor.line]);
                                    cursor.index++;
                                } else if (cursor.line < text->lineCount - 1) {
                                    cursor.line++;
                                    cursor.index = 0;
                                }
                                
                                selection.end_line = cursor.line;
                                selection.end_index = cursor.index;
                                cursor.preferred_x = calculateCursorX(text->lines[cursor.line], glyphMap, cursor.index);
                            } else {
                                if (selection.start_line != selection.end_line || selection.start_index != selection.end_index) {
                                    cursor.line = selection.end_line;
                                    cursor.index = selection.end_index;
                                } else if (cursor.index < gapUsed(text->lines[cursor.line])) {
                                    cursorRight(text->lines[cursor.line]);
                                    cursor.index++;
                                } else if (cursor.line < text->lineCount - 1) {
                                    cursor.line++;
                                    cursor.index = 0;
                                }
                                selection.start_line = selection.end_line = cursor.line;
                                selection.start_index = selection.end_index = cursor.index;
                                cursor.preferred_x = calculateCursorX(text->lines[cursor.line], glyphMap, cursor.index);
                            }
                            break;

                        case SDLK_UP:
                            if (shift_pressed) {
                                if (selection.start_line == selection.end_line && 
                                    selection.start_index == selection.end_index) {
                                    selection.start_line = cursor.line;
                                    selection.start_index = cursor.index;
                                }
                                
                                if (cursor.line > 0) {
                                    cursor.line--;
                                    cursor.index = findCursorPosition(text->lines[cursor.line], glyphMap, cursor.preferred_x);
                                }
                                
                                selection.end_line = cursor.line;
                                selection.end_index = cursor.index;
                            } else {
                                if (cursor.line > 0) {
                                    cursor.line--;
                                    cursor.index = findCursorPosition(text->lines[cursor.line], glyphMap, cursor.preferred_x);
                                }
                                selection.start_line = selection.end_line = cursor.line;
                                selection.start_index = selection.end_index = cursor.index;
                            }
                            break;

                        case SDLK_DOWN:
                            if (shift_pressed) {
                                if (selection.start_line == selection.end_line && 
                                    selection.start_index == selection.end_index) {
                                    selection.start_line = cursor.line;
                                    selection.start_index = cursor.index;
                                }
                                
                                if (cursor.line < text->lineCount - 1) {
                                    cursor.line++;
                                    cursor.index = findCursorPosition(text->lines[cursor.line], glyphMap, cursor.preferred_x);
                                }
                                
                                selection.end_line = cursor.line;
                                selection.end_index = cursor.index;
                            } else {
                                if (cursor.line < text->lineCount - 1) {
                                    cursor.line++;
                                    cursor.index = findCursorPosition(text->lines[cursor.line], glyphMap, cursor.preferred_x);
                                }
                                selection.start_line = selection.end_line = cursor.line;
                                selection.start_index = selection.end_index = cursor.index;
                            }
                            break;
                case SDLK_PAGEUP:
                    scroll.y = MAX(0, scroll.y - (scroll.win_h / glyphMap->glyphHeight));
                    break;

                case SDLK_PAGEDOWN:
                    scroll.y = MIN(scroll.max_y, scroll.y + (scroll.win_h / glyphMap->glyphHeight));
                    break;
                }

                // Keep cursor visible vertically
                int lines_visible = scroll.win_h / glyphMap->glyphHeight;
                if ((int)cursor.line < scroll.y)
                {
                    scroll.y = (int)cursor.line;
                }
                else if ((int)cursor.line >= scroll.y + lines_visible)
                {
                    scroll.y = (int)cursor.line - lines_visible + 1;
                }

                // Keep cursor visible horizontally
                int cursor_x = calculateCursorX(text->lines[cursor.line], glyphMap, cursor.index);
                if (cursor_x < scroll.x)
                {
                    scroll.x = MAX(0, cursor_x - 20);
                }
                else if (cursor_x > scroll.x + scroll.win_w - 20)
                {
                    scroll.x = MIN(scroll.max_x, cursor_x - scroll.win_w + 20);
                }
                break;

                    case SDL_KEYUP:
                    switch (event.key.keysym.sym) {
                        case SDLK_LSHIFT:
                        case SDLK_RSHIFT:
                            shift_pressed = false;
                            break;
                    }
                    break;
            }
        }

        sdl_cc(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0));
        sdl_cc(SDL_RenderClear(renderer));
        renderText(renderer, text, &cursor, &selection, fontTexture, cursorTexture, color, glyphMap, &scroll);
        SDL_RenderPresent(renderer);
    }

    freeText(text);
    freeGlyphMap(glyphMap);
    SDL_DestroyTexture(cursorTexture);
    SDL_DestroyTexture(fontTexture);
    TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
