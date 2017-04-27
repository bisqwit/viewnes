#include <SDL.h>
#include <vector>
#include <tuple>
#include <cstdio>
#include <signal.h>
#include <chrono>
#include <cmath>
#include <string>
#include <cstring>

#include <unistd.h>

class UIfontBase { };
#include "font/6x9.inc"
#include "font/8x16.inc"

#include "crc32.h"
#include "mario.hh"

template<typename T>
static T constexpr constmin(T a, T b) { return a<b ? a : b; }
template<typename T>
static T constexpr constmax(T a, T b) { return a>b ? a : b; }

static constexpr unsigned FontWidth      = 6;
static constexpr unsigned FontHeight     = 9;

static constexpr unsigned LeftWidth     = FontWidth*17;
static constexpr unsigned LeftMargin    = 4;

static constexpr unsigned CharsPerLine  = 32;
static constexpr unsigned HexViewWidth  =
    CharsPerLine * FontWidth*2
  + (CharsPerLine/16) * (5)
  + (CharsPerLine/4 - CharsPerLine/16)  * (3)
  + (CharsPerLine - CharsPerLine/4 - CharsPerLine/16) * 1;

static constexpr unsigned TextLeftMargin   = 1;
static constexpr unsigned TextViewWidth    = CharsPerLine * FontWidth;
static constexpr unsigned TextRightMargin  = 4;

static constexpr unsigned GFXviewWidth  = 256;
static constexpr unsigned GFXviewHeight = 256;
static constexpr unsigned GFXviewScale  = 2;

// A 16x16 box of tiles (128*128 pixels) is 0x1000 bytes.
// 0x1000 bytes translates into 32x128 characters of text,
// i.e. 640x1152 pixels at our select font size.
// We could render that data at 4x scaling.

static constexpr unsigned ROMpageSize  = 16384;
static constexpr unsigned VROMpageSize = 8192;
static constexpr unsigned DflWidth =
    LeftWidth
  + LeftMargin
  + HexViewWidth
  + constmax(TextLeftMargin + TextViewWidth + TextRightMargin + 32*GFXviewScale, GFXviewWidth&0);

static constexpr unsigned DflHeight =
    //FontHeight * (0x600/CharsPerLine)
    //DflWidth*9/16
    480
    ;

static constexpr unsigned StatusWidth = DflWidth / 9;
static constexpr unsigned StatusMargin = (DflWidth % 9) / 2;

static const char hexbytes[16] =
    {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};

/*
01234567890123456 = 17 (left width)
00013AAF(00:FAAF)
*/

static unsigned char transliterate = 0, transliterate2 = 0;
static unsigned      mousey        = 0, mousex = 0;
static unsigned FirstLineLength = 0;//16;
static unsigned NumHeaderLines  = 0;//1;

static bool TallSprites = false;

class ROMviewer
{
public:
    struct
    {
        unsigned n_rom16k;
        unsigned n_vrom8k;
    } header;

    std::vector<unsigned char> image;

    SDL_Window*   window;
    SDL_Renderer* renderer;
    SDL_Texture*  texture;
    std::vector<uint32_t> framebuffer;
    unsigned ScrollBegin;
public:
    ROMviewer(const std::vector<unsigned char>& romdata) : image(romdata)
    {
        SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);
        SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);
        SDL_EventState(SDL_KEYUP, SDL_IGNORE); // Ignore keyup events

        window = SDL_CreateWindow("hex viewer",
                                  SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                  DflWidth*2, DflHeight*2, SDL_WINDOW_RESIZABLE);
        renderer = SDL_CreateRenderer(window, -1, 0);
        texture  = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, DflWidth,DflHeight);
        framebuffer.resize(DflWidth*DflHeight);

        printf("Makes window of %ux%u; aspect ratio %.4f\n", DflWidth,DflHeight, DflWidth*1.0/DflHeight);
        signal(SIGINT, SIG_DFL);

        if(image[0]=='N' && image[1]=='E' && image[2]=='S' && image[3]==0x1A)
        {
            header.n_rom16k = image[0x04];
            header.n_vrom8k = image[0x05];
            FirstLineLength = 16;
            NumHeaderLines = 1;
        }
        else
        {
            header.n_rom16k = 0;
            header.n_vrom8k = 0;
            FirstLineLength = 0;
            NumHeaderLines = 0;
        }

        ScrollBegin = 0;
    }

    std::size_t GetBeginOffset(unsigned line) const
    {
        if(line == 0) return 0;
        return FirstLineLength + (line-NumHeaderLines) * CharsPerLine;
    }
    std::pair<size_t,size_t> GetROMaddr(unsigned line) const
    {
        return GetROMaddrForOffset(GetBeginOffset(line));
    }
    std::pair<size_t,size_t> GetROMaddrForOffset(std::size_t offset) const
    {
        if(offset == 0) return {0,0};
        offset -= FirstLineLength;
        if(offset < header.n_rom16k * ROMpageSize)
        {
            unsigned pageno = offset / ROMpageSize, pageptr = offset % ROMpageSize;
            return { pageno, pageptr + ((pageno+1==header.n_rom16k) ? 0xC000 : 0x8000) };
        }
        offset -= header.n_rom16k * ROMpageSize;
        unsigned pageno = offset / VROMpageSize, pageptr = offset % VROMpageSize;
        return { pageno, pageptr };
    }
    void RenderLine(unsigned yoffset)
    {
        if(yoffset >= DflHeight) return;

        uint32_t* scanline = &framebuffer[0] + yoffset * DflWidth;

        if(yoffset < 16)
        {
            for(unsigned x=0; x<StatusWidth; ++x)
                PutBigChar(scanline + x*9, yoffset, x < Status.size() ? Status[x] : ' ', 0xAAAAAA, 0x0000AA);
        }
        else if(yoffset >= DflHeight - 16)
        {
            yoffset -= (DflHeight - 16);

            for(unsigned x=0; x<StatusWidth; ++x)
                PutBigChar(scanline + x*9, yoffset, x < Bottom.size() ? Bottom[x] : ' ', 0x000000, 0x00AAAA);

            const unsigned room_left   = 240;
            const unsigned room_right  = 8;
            const unsigned room_wide   = StatusWidth * 8;
            const unsigned xspanlength = room_wide + room_left + room_right;
            unsigned long mt = MarioTimer / 2;
            const unsigned MarioStepInterval = 7;
            const unsigned poses = 2u;
            unsigned marioframe = (mt / MarioStepInterval) % poses;
            if(marioframe == 3) marioframe = 1;
            int mariox = (mt % xspanlength) - room_left;
            const unsigned on = 0x555555;
            int beginx = mariox & ~7;
            int endx   = (mariox + 16) | 7;

            for(int xp=beginx; xp<=endx; ++xp)
            {
                if(xp >= 0 && xp < int(StatusWidth*8))
                {
                    uint32_t* s = scanline + (xp/8)*9 + (xp%8);
                    unsigned char c = GetMarioBit(marioframe, xp,yoffset, xp-mariox, 16);
                    if(c & 2)
                    {
                        *s = (c & 1) ? on : 0x00AAAAA;
                        if((xp & 7) == 7)
                            s[1] = (c & 1) ? on : 0x00AAAAA;
                    }
                    else if(*s == 0)
                        *s = on;
                }
            }
        }
        else
        {
            RenderDumpLine(scanline, yoffset + ScrollBegin - 16);
        }
    }

    void RenderDumpLine(uint32_t* scanline, unsigned yoffset)
    {
        unsigned line = yoffset / FontHeight, pixoffset = yoffset % FontHeight;
        unsigned BeginOffset = GetBeginOffset(line);

        if(BeginOffset >= image.size())
        {
            std::fill_n(scanline, DflWidth, 0x488888);
            return;
        }

        RenderLeft(scanline, BeginOffset, pixoffset);
        RenderHex(scanline,  BeginOffset, pixoffset);

        if(BeginOffset < FirstLineLength + header.n_rom16k * ROMpageSize)
        {
            RenderText(scanline, BeginOffset, pixoffset);
        }
        else
        {
            unsigned NonVROMsize = FirstLineLength + header.n_rom16k * ROMpageSize;
            // How many lines does non-VROM take?
            unsigned NonVROMlines = NumHeaderLines + (NonVROMsize - FirstLineLength) / CharsPerLine;
            // How many bytes into VROM are we?
            unsigned GFXoffset          = BeginOffset - NonVROMsize;
            // Where does THIS VROM page begin?
            unsigned GFXpageBeginOffset = GFXoffset & ~0xFFF;

            // Which pixel-yoffset does that page begin at?
            unsigned line_that_begins_block =
                NonVROMlines + GFXpageBeginOffset / CharsPerLine;

            unsigned ypixel_that_begins_block =
                FontHeight * line_that_begins_block;

            // Which y-pixel are we going at, relative to the beginning of this block?
            unsigned ypixel_relative = yoffset - ypixel_that_begins_block;
            //fprintf(stderr, "At offset=%08X, yoffset=%u, ypixel_relative=%u, pagebegin=%X\n",
            //    BeginOffset, yoffset, ypixel_relative, GFXpageBeginOffset);

            if(ypixel_relative >= GFXviewHeight)
            {
                std::fill_n(scanline + LeftWidth + LeftMargin + HexViewWidth, GFXviewWidth, 0x888888);
            }
            else
            {
                // 16 tiles per scanline. 16 bytes per tile.
                unsigned ypixel_unscale = ypixel_relative / GFXviewScale;

                // rom(tileno,scanline) = {byte1:tileno*16+scanline, byte2:tileno*16+8+scanline}
                // tileno(x,y)          = {tileno: (x/8) + (y/8)*16, scanline:y%8 }
                // rom(x,y)             = {byte1:((x/8) + (y/8)*16)*16 +     y%8,
                //                         byte2:((x/8) + (y/8)*16)*16 + 8 + y%8}
                unsigned skip = LeftWidth + LeftMargin + HexViewWidth;
                scanline += skip;
                RenderGFX(scanline,
                          NonVROMsize + GFXpageBeginOffset + (ypixel_unscale/8)*16*16 + (ypixel_unscale%8),
                          GFXviewWidth);

                std::fill_n(scanline+GFXviewWidth, DflWidth - skip-GFXviewWidth, 0x000000);
            }
        }
    }
private:
    void PutBigChar(uint32_t* buffer, unsigned whichline,
                    char32_t which_unichar, unsigned color, unsigned bgcolor=0)
    {
        using namespace ns_font8x16;
        const font8x16 f;

        const unsigned index = f.GetIndex(which_unichar);
        //printf("Char index %04X -> %04X\n", which_unichar, index);
        const auto b = f.GetBitmap() + index * 16;

        unsigned char c = b[whichline];

        for(unsigned x=0; x<9; ++x)
            buffer[x] = (c & (0x80 >> x)) ? color : bgcolor;
    }
    void PutChar(uint32_t* buffer, unsigned whichline,
                 char32_t which_unichar, unsigned color, unsigned bgcolor=0)
    {
        using namespace ns_font6x9;
        const font6x9 f;

        const unsigned index = f.GetIndex(which_unichar);
        //printf("Char index %04X -> %04X\n", which_unichar, index);
        const auto b = f.GetBitmap() + index * FontHeight;

        unsigned char c = b[whichline];

        for(unsigned x=0; x<FontWidth; ++x)
            buffer[x] = (c & (0x80 >> x)) ? color : bgcolor;
    }
    void RenderLeft(uint32_t* scanline, unsigned ROMoffset, unsigned whichline)
    {
        if(whichline >= FontHeight)
            std::fill_n(scanline, LeftWidth, 0x404040);
        else
        {
            unsigned ROMpage, ROMoffs;
            std::tie(ROMpage,ROMoffs) = GetROMaddrForOffset(ROMoffset);

            char Buf[64];
            std::sprintf(Buf,"%08X(%02X:%04X)", ROMoffset, ROMpage, ROMoffs);
            for(unsigned p=0, x=0; p<LeftWidth/FontWidth; x+=FontWidth, ++p)
                PutChar(scanline+x, whichline, Buf[p], 0xFFFFFF);
        }
    }
    void RenderHex(uint32_t* scanline, unsigned ROMoffset, unsigned whichline)
    {
        unsigned w = (!FirstLineLength || ROMoffset) ? CharsPerLine : FirstLineLength;

        scanline += LeftWidth;

        std::fill_n(scanline, LeftMargin, 0x000000);

        scanline += LeftMargin;

        unsigned x=0;
        for(unsigned p=0; p<w; ++p)
        {
            unsigned color   = (p&4) ? 0xCCCCCC : 0xD0D0D0;
            unsigned bgcolor = (p&4) ? 0x000000 : 0x000000;

            PutChar(scanline+x,           whichline,
                (unsigned char) hexbytes[image[ROMoffset+p] >>4],
                color, bgcolor);
            PutChar(scanline+x+FontWidth, whichline,
                (unsigned char) hexbytes[image[ROMoffset+p]&0xF],
                color, bgcolor);

            unsigned space = (p+1)%16 == 0 ? 5 : ((p+1)%4 == 0 ? 3 : 1);

            x += FontWidth*2;

            std::fill_n(scanline+x, space, 0x000000);

            x += space;
        }

        if(x < HexViewWidth)
            std::fill_n(scanline + x, (HexViewWidth-x), 0x888888);
    }
    void RenderText(uint32_t* scanline, unsigned ROMoffset, unsigned whichline)
    {
static const unsigned cp437[256] =
{
  0x0000,0x0001,0x0002,0x0003,0x0004,0x0005,0x0006,0x0007,0x0008,0x0009,0x000a,0x000b,0x000c,0x000d,0x000e,0x000f,0x0010,0x0011,0x0012,0x0013,0x0014,0x0015,0x0016,0x0017,0x0018,0x0019,0x001a,0x001b,0x001c,0x001d,0x001e,0x001f,0x0020,0x0021,0x0022,0x0023,0x0024,0x0025,0x0026,0x0027,0x0028,0x0029,0x002a,0x002b,0x002c,0x002d,0x002e,0x002f,0x0030,0x0031,0x0032,0x0033,0x0034,0x0035,0x0036,0x0037,0x0038,0x0039,0x003a,0x003b,0x003c,0x003d,0x003e,0x003f,0x0040,0x0041,0x0042,0x0043,0x0044,0x0045,0x0046,0x0047,0x0048,0x0049,0x004a,0x004b,0x004c,0x004d,0x004e,0x004f,0x0050,0x0051,0x0052,0x0053,0x0054,0x0055,0x0056,0x0057,0x0058,0x0059,0x005a,0x005b,0x005c,0x005d,0x005e,0x005f,0x0060,0x0061,0x0062,0x0063,0x0064,0x0065,0x0066,0x0067,0x0068,0x0069,0x006a,0x006b,0x006c,0x006d,0x006e,0x006f,0x0070,0x0071,0x0072,0x0073,0x0074,0x0075,0x0076,0x0077,0x0078,0x0079,0x007a,0x007b,0x007c,0x007d,0x007e,0x007f,0x00c7,0x00fc,0x00e9,0x00e2,0x00e4,0x00e0,0x00e5,0x00e7,0x00ea,0x00eb,0x00e8,0x00ef,0x00ee,0x00ec,0x00c4,0x00c5,0x00c9,0x00e6,0x00c6,0x00f4,0x00f6,0x00f2,0x00fb,0x00f9,0x00ff,0x00d6,0x00dc,0x00a2,0x00a3,0x00a5,0x20a7,0x0192,0x00e1,0x00ed,0x00f3,0x00fa,0x00f1,0x00d1,0x00aa,0x00ba,0x00bf,0x2310,0x00ac,0x00bd,0x00bc,0x00a1,0x00ab,0x00bb,0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255d,0x255c,0x255b,0x2510,0x2514,0x2534,0x252c,0x251c,0x2500,0x253c,0x255e,0x255f,0x255a,0x2554,0x2569,0x2566,0x2560,0x2550,0x256c,0x2567,0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256b,0x256a,0x2518,0x250c,0x2588,0x2584,0x258c,0x2590,0x2580,0x03b1,0x00df,0x0393,0x03c0,0x03a3,0x03c3,0x00b5,0x03c4,0x03a6,0x0398,0x03a9,0x03b4,0x221e,0x03c6,0x03b5,0x2229,0x2261,0x00b1,0x2265,0x2264,0x2320,0x2321,0x00f7,0x2248,0x00b0,0x2219,0x00b7,0x221a,0x207f,0x00b2,0x25a0,0x00a0
};
        unsigned pre = LeftWidth + LeftMargin + HexViewWidth;
        scanline += pre;

        std::fill_n(scanline, TextLeftMargin, 0x00000);

        unsigned gx = 16 * GFXviewScale;

        pre += TextLeftMargin;
        scanline += TextLeftMargin;

        unsigned w = (!FirstLineLength || ROMoffset) ? CharsPerLine : FirstLineLength;
        for(unsigned p=0, x=0; p<w; x+=FontWidth, ++p)
        {
            unsigned color   = (p&4) ? 0xCCCCCC : 0xD0D0D0;
            unsigned bgcolor = (p&4) ? 0x000050 : 0x000000;
            unsigned c = (image[ROMoffset+p] + transliterate) & 0xFF;
            if(c+transliterate2 >= 'a' && c+transliterate2 <= 'z') c += transliterate2;

            c = cp437[c];
            if( (c >= 'A' && c <= 'Z')
             || (c >= 'a' && c <= 'z')
             || (c >= '0' && c <= '9') )
                color = 0xF0F055;
            if(c < 0x20)
            {
                if(c == 0)
                    c = '.';
                else
                    c = 0x2660 + (c&0x1F);
                color = 0xA07010;
            }
            else if(c >= 0x80)
            {
                //c = 0x100+(c&0x7F);
                color = 0xA050EF;
            }

            PutChar(scanline+x, whichline, c, color, bgcolor);
        }

        std::fill_n(scanline+w*FontWidth, (CharsPerLine-w)*FontWidth + TextRightMargin, 0x000000);
        pre += CharsPerLine*FontWidth + TextRightMargin;
        scanline += CharsPerLine*FontWidth + TextRightMargin;

        // 32 bytes corresponds to two tiles.
        // We render tiles at 16x16 size.
        if(ROMoffset >= FirstLineLength)
        {
            unsigned l1 = whichline, offs1 = ROMoffset;
            unsigned l2 = whichline, offs2 = ROMoffset;
            unsigned TileSize = TallSprites ? 0x20 : 0x10;
            if( !( (ROMoffset-FirstLineLength) & TileSize) )
            {
                if(offs2 >= TileSize) offs2 -= TileSize;
            }
            else
            {
                if(FirstLineLength) { l1 += FontHeight; l2 += FontHeight; }
                if(offs1 >= TileSize) offs1 -= TileSize;
            }

            if(l1 < GFXviewScale*8)
                RenderGFX(scanline,    offs1 + l1/GFXviewScale, gx);
            else
                std::fill_n(scanline, gx, 0x888888);

            if(l2 < GFXviewScale*8)
                RenderGFX(scanline+gx, offs2 + l2/GFXviewScale, gx);
            else
                std::fill_n(scanline+gx, gx, 0x888888);

            pre += 2*gx;
            scanline += 2*gx;
            if(pre < DflWidth)
                std::fill_n(scanline, DflWidth - pre, 0x000000);
        }
        else
        {
            if(pre < DflWidth)
                std::fill_n(scanline, DflWidth - pre, 0x000000);
        }
    }
    void RenderGFX(uint32_t* scanline, unsigned ROMoffset, unsigned n_pixels)
    {
        //static const unsigned colors[4] = {0x000000,0x3333FF,0xFFFFFF,0xFF556B};
        static const unsigned colors[4] = {0x000000,
          0xFF556B, // red
          0xFFFFFF, // white
          0x3333FF, // blue
        };

        if(TallSprites && (ROMoffset & 0x100))
        {
            ROMoffset -= 0x100;
            ROMoffset += 0x10; // TODO: Figure out what is the purpose here
        }

        for(unsigned x=0; x<n_pixels; x+=GFXviewScale*8)
        {
            //unsigned o = ROMoffset;

            unsigned char byte1 = image[ROMoffset], byte2 = image[ROMoffset+8];
            for(unsigned p=0; p<8; ++p)
            {
                bool bit1 = byte1 & (0x80 >> p);
                bool bit2 = byte2 & (0x80 >> p);
                unsigned char pix = bit1 + bit2*2;
                std::fill_n(scanline + x + p*GFXviewScale, GFXviewScale, colors[pix]);
            }
            ROMoffset += TallSprites ? 32 : 16;
        }
    }
public:
    unsigned dirtyscan = 0, dirty_scanned_without_hit = 0;
    std::vector<bool> dirty_lines;
    std::vector<bool> in_need_of_refreshing;
    bool fresh = false;

    std::string Status, Bottom;

    std::chrono::time_point<std::chrono::system_clock>
        last_refresh = std::chrono::system_clock::now();

    void MakeDirty()
    {
        dirty_lines.resize( DflHeight, false );
        for(unsigned y=0; y< DflHeight ; ++y)
            dirty_lines[y] = true;
        dirty_scanned_without_hit = 0;

        char Buf[StatusWidth*2];
        std::sprintf(Buf, "ROM size: %u x 16kB ROM, %u x 8kB VROM; 'A' is assumed to be %02X, 'a' to be %02X",
            header.n_rom16k,
            header.n_vrom8k,
            ('A' - transliterate) & 0xFF,
            ('a' - transliterate - transliterate2) & 0xFF
        );
        Status = Buf;
    }
    void MakeStatusDirty()
    {
        dirty_lines.resize( DflHeight, false );
        for(unsigned y=0; y<16 ; ++y)
        {
            dirty_lines[y] = true;
            dirty_lines[(DflHeight - 16) + y] = true;
        }
        dirty_scanned_without_hit = 0;

        bool clear = false;
        unsigned ROMoffset = 0;
        if(mousey < 16 || mousey >= (DflHeight - 16))
            clear = true;
        else
        {
            unsigned line = (mousey-16 + ScrollBegin) / FontHeight;
            ROMoffset = GetBeginOffset(line);
            int mx = mousex;

            if(mx < int(LeftWidth+LeftMargin))
                clear = true;
            else
            {
                mx -= LeftWidth+LeftMargin;
                if(mx < int(HexViewWidth))
                {
                    /*
                        xcoordinate = byteindex * (f*2+1) + (byteindex/4)*2 + (byteindex/16)*2;
                        solve for xcoordinate gives   8*xcoordinate / (16*f+13)
                    */
                    unsigned x = 8*mx / (16*FontWidth + 13);
                    ROMoffset += x;
                }
                else
                {
                    mx -= HexViewWidth + TextLeftMargin;
                    if(mx >= 0 && mx < int(TextViewWidth))
                        ROMoffset += mx / FontWidth;
                    else
                        clear = true;
                }
            }
        }
        if(clear)
            Bottom.clear();
        else
        {
            unsigned ROMpage, ROMoffs;
            std::tie(ROMpage,ROMoffs) = GetROMaddrForOffset(ROMoffset);

            char Buf[StatusWidth*2];
            std::sprintf(Buf, "%08X(%02X:%04X) (byte at this location: %02X %02X <%02X> %02X %02X)",
                ROMoffset,
                ROMpage,
                ROMoffs,
                ROMoffset>=2 ? image[ROMoffset-2] : 0xFF,
                ROMoffset>=1 ? image[ROMoffset-1] : 0xFF,
                image[ROMoffset  ],
                (ROMoffset+1) < image.size() ? image[ROMoffset+1] : 0xFF,
                (ROMoffset+2) < image.size() ? image[ROMoffset+2] : 0xFF
            );
            Bottom = Buf;
        }
    }

    void MakeMarioDirty()
    {
        dirty_lines.resize( DflHeight, false );
        for(unsigned y=0; y<16 ; ++y)
            dirty_lines[(DflHeight - 16) + y] = true;
    }

    void Refresh_Update()
    {
        if(fresh) return;

        auto now = std::chrono::system_clock::now();
        bool timeout = std::chrono::duration_cast<std::chrono::milliseconds>(now-last_refresh).count() > 200;

        if(timeout || IsClean())
        {
            /*std::vector<SDL_Rect> rects;
            bool on = false;
            for(unsigned y=0; y< DflHeight; ++y)
            {
                if(in_need_of_refreshing[y]
                || (on && in_need_of_refreshing[(y+1) % DflHeight])
                || (on && in_need_of_refreshing[(y+2) % DflHeight])
                  )
                {
                    if(!on)
                        { on = true; rects.push_back( { 0, (Sint16) y, (Uint16) DflWidth, 1 } ); }
                    else
                        ++rects.back().h;
                }
                else
                    on = false;
            }
            if(!rects.empty())*/
            {
                //fprintf(stderr, "Updates %u rects\n", (unsigned) rects.size() );
                void* pixels;
                int   pitch;
                if(SDL_LockTexture(texture, nullptr, &pixels, &pitch) >= 0)
                {
                    /*for(const auto& r: rects)
                    {
                        for(unsigned y=0; y<r.h; ++y)
                            std::memcpy((Uint8*)pixels + (r.y+y) * pitch,
                                        &framebuffer[(r.y+y) * DflWidth],
                                        std::min(pitch, int(DflWidth*sizeof(Uint32*))));
                    }*/
                    for(unsigned y=0; y<DflHeight; ++y)
                        std::memcpy((Uint8*)pixels + (y) * pitch,
                                    &framebuffer[(y) * DflWidth],
                                    std::min(pitch, int(DflWidth*sizeof(Uint32*))));

                    SDL_UnlockTexture(texture);
                    //for(const auto& r: rects)
                    //    SDL_RenderCopy(renderer, texture, &r, &r);
                    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
                    //SDL_UpdateRects(s, rects.size(), &rects[0]);
                    SDL_RenderPresent(renderer);

                    std::fill(in_need_of_refreshing.begin(), in_need_of_refreshing.end(), false);
                    last_refresh = std::chrono::system_clock::now();
                    fresh = true;
                }
            }
        }
    }

    static crc32_t CheckSum(const void* p, std::size_t n)
    {
        return crc32_calc( (const unsigned char*) p, n );
    }

    void Refresh()
    {
        in_need_of_refreshing.resize( DflHeight, false );

        while(!dirty_lines[dirtyscan])
        {
            if(IsClean())
            {
                Refresh_Update();
                return;
            }

            ++dirty_scanned_without_hit;
            if(++dirtyscan == DflHeight) dirtyscan = 0; // wrap around
        }

        dirty_lines[dirtyscan]           = false;
        fresh = false;
        auto checksum_before = CheckSum( &framebuffer[0] + dirtyscan * DflWidth, DflWidth*4 );
        RenderLine(dirtyscan);
        auto checksum_after  = CheckSum( &framebuffer[0] + dirtyscan * DflWidth, DflWidth*4 );

        if(checksum_before != checksum_after)
            in_need_of_refreshing[dirtyscan] = true;

        if(++dirtyscan == DflHeight) dirtyscan = 0; // wrap around
        dirty_scanned_without_hit = 1;

        Refresh_Update();
    }

    void BeginSearchDialog()
    {
        /*
            Search modes:
                Text search
                    With or without transpositions
                Hex word(s) search
                    Which pages
        */
    }

    bool IsClean() const
    {
        return dirty_scanned_without_hit >= (DflHeight);
    }
};

static void DefineMouseCursor()
{
    static Uint8 mask[19*2] =
    {
        0b10000000,0b00000000,
        0b11000000,0b00000000,
        0b11100000,0b00000000,
        0b11110000,0b00000000,
        0b11111000,0b00000000,
        0b11111100,0b00000000,
        0b11111110,0b00000000,
        0b11111111,0b00000000,
        0b11111111,0b10000000,
        0b11111111,0b11000000,
        0b11111111,0b11100000,
        0b11111110,0b00000000,
        0b11101111,0b00000000,
        0b11001111,0b00000000,
        0b10000111,0b10000000,
        0b00000111,0b10000000,
        0b00000011,0b11000000,
        0b00000011,0b11000000,
        0b00000001,0b10000000
    };
    static Uint8 data[19*2] =
    {
        0b10000000,0b00000000,
        0b11000000,0b00000000,
        0b10100000,0b00000000,
        0b10010000,0b00000000,
        0b10001000,0b00000000,
        0b10000100,0b00000000,
        0b10000010,0b00000000,
        0b10000001,0b00000000,
        0b10000000,0b10000000,
        0b10000000,0b01000000,
        0b10000011,0b11100000,
        0b10010010,0b00000000,
        0b10101001,0b00000000,
        0b11001001,0b00000000,
        0b10000100,0b10000000,
        0b00000100,0b10000000,
        0b00000010,0b01000000,
        0b00000010,0b01000000,
        0b00000001,0b10000000
    };
    SDL_SetCursor(SDL_CreateCursor(data,mask,16,19,0,0));
}

int main(int argc, char** argv)
{
    std::FILE* fp = std::fopen(argv[1], "rb");
    std::fseek(fp, 0, SEEK_END);
    long size = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);

    std::vector<unsigned char> data( size );
    std::fread(&data[0], 1, data.size(), fp);
    std::fclose(fp);

    ROMviewer viewer( data );

    viewer.MakeDirty();

    //SDL_EnableKeyRepeat(250, 1000/60);
    //SDL_EnableUNICODE(1);

    std::chrono::time_point<std::chrono::system_clock>
        last_slow = std::chrono::system_clock::now();

    DefineMouseCursor();
    SDL_ShowCursor(1);

    std::chrono::time_point<std::chrono::system_clock>
        timer_begin = std::chrono::system_clock::now();

    SDL_StartTextInput();
    //SDL_StopTextInput();

    double scroll_pos = 0, aim_pos = 0, last_pos = 0;
    for(;;)
    {
        viewer.MakeMarioDirty();
        MarioTimer =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now() - timer_begin).count() * 3 / 40; // 75 Hz

        for(unsigned n=0; n<32; ++n)
            viewer.Refresh();

        SDL_Event event = { };

        bool avail = SDL_PollEvent( &event );

        bool scroll = false;
        const unsigned viewport = DflHeight - 2*16;

        if(!avail && (viewer.IsClean() && scroll_pos == aim_pos))
        {
            viewer.MakeStatusDirty();
            usleep(45000);
            continue;
        }

        if(avail)
        switch(event.type)
        {
            case SDL_TEXTINPUT:
                switch(event.text.text[0])
                {
                    case '+':
                        transliterate -= 1;
                        viewer.MakeDirty();
                        break;
                    case '-':
                        transliterate += 1;
                        viewer.MakeDirty();
                        break;
                    case '(':
                        transliterate2 -= 1;
                        viewer.MakeDirty();
                        break;
                    case ')':
                        transliterate2 += 1;
                        viewer.MakeDirty();
                        break;
                    case '*':
                        transliterate = 0;
                        viewer.MakeDirty();
                        break;
                    case ' ':
                        goto pgdn;
                    case '/':
                        viewer.BeginSearchDialog();
                        break;
                    case '>': // big pagedown
                    {
                        long offs_now  = viewer.GetBeginOffset(aim_pos / FontHeight + 0.5);
                        if(offs_now >= FirstLineLength) offs_now -= FirstLineLength;
                        long gfx_begin = viewer.header.n_rom16k * ROMpageSize;
                        double aim;
                        if(offs_now >= gfx_begin)
                            aim = (offs_now + 0x1000) & ~0xFFF;
                        else
                            aim = (offs_now + 0x4000) & ~0x3FF;
                        fprintf(stderr, "At %08lX, aiming for %08lX\n", offs_now+FirstLineLength, (long)aim + FirstLineLength);

                        aim *= double(FontHeight) / double(CharsPerLine);
                        aim += NumHeaderLines*FontHeight;

                        aim_pos = aim;
                        scroll = true;
                        break;
                    }
                    case '<': // big pageup
                    {
                        long offs_now = viewer.GetBeginOffset(aim_pos / FontHeight + 0.5);
                        if(offs_now >= FirstLineLength) offs_now -= FirstLineLength;
                        long gfx_begin = viewer.header.n_rom16k * ROMpageSize;
                        double aim;
                        if(offs_now > gfx_begin)
                            aim = ((offs_now & 0xFFF) ? offs_now &~ 0xFFF : (offs_now-0x1000));
                        else
                            aim = ((offs_now & 0x3FFF) ? offs_now &~ 0x3FFF : (offs_now - 0x4000));
                        fprintf(stderr, "At %08lX, aiming for %08lX\n", offs_now+FirstLineLength, (long)aim + FirstLineLength);

                        aim *= double(FontHeight) / double(CharsPerLine);
                        aim += NumHeaderLines*FontHeight;

                        aim_pos = aim;
                        scroll = true;
                        break;
                    }
                    case 't':
                    {
                        TallSprites = !TallSprites;
                        viewer.MakeDirty();
                        break;
                    }
                    case 'e': goto k_e;
                    case 'a': goto k_a;
                    case 'v': goto pgdn;
                    case 'u': goto pgup;
                    case 's': goto k_s;
                    case 'w': goto k_w;
                }
                break;
            case SDL_KEYDOWN:
                //if(viewer.InDialog())
                {
                }
                switch(event.key.keysym.sym)
                {
                    case SDLK_UP: k_w:
                        aim_pos -= FontHeight;
                        scroll = true;
                        break;
                    case SDLK_DOWN: k_s:
                        aim_pos += FontHeight;
                        scroll = true;
                        break;
                    case SDLK_PAGEUP: pgup:
                    {
                        long offs_now = viewer.GetBeginOffset(aim_pos / FontHeight + 0.5);
                        if(offs_now >= FirstLineLength) offs_now -= FirstLineLength;
                        long gfx_begin = viewer.header.n_rom16k * ROMpageSize;
                        double aim;
                        if(offs_now > gfx_begin)
                            aim = ((offs_now & 0xFFF) ? offs_now &~ 0xFFF : (offs_now-0x1000));
                        else
                            aim = ((offs_now & 0x3FF) ? offs_now &~ 0x3FF : (offs_now - 0x400));
                        fprintf(stderr, "At %08lX, aiming for %08lX\n", offs_now+FirstLineLength, (long)aim + FirstLineLength);

                        aim *= double(FontHeight) / double(CharsPerLine);
                        aim += NumHeaderLines*FontHeight;

                        aim_pos = aim;
                        scroll = true;
                        break;
                    }
                    case SDLK_PAGEDOWN: pgdn:
                    {
                        long offs_now  = viewer.GetBeginOffset(aim_pos / FontHeight + 0.5);
                        if(offs_now >= FirstLineLength) offs_now -= FirstLineLength;
                        long gfx_begin = viewer.header.n_rom16k * ROMpageSize;
                        double aim;
                        if(offs_now >= gfx_begin)
                            aim = (offs_now + 0x1000) & ~0xFFF;
                        else
                            aim = (offs_now + 0x400) & ~0x3FF;
                        fprintf(stderr, "At %08lX, aiming for %08lX\n", offs_now+FirstLineLength, (long)aim + FirstLineLength);

                        aim *= double(FontHeight) / double(CharsPerLine);
                        aim += NumHeaderLines*FontHeight;

                        aim_pos = aim;
                        scroll = true;
                        break;
                    }
                    case SDLK_HOME: k_a:
                    {
                        long offs_now  = viewer.GetBeginOffset(aim_pos / FontHeight + 0.5);
                        long rom_begin = FirstLineLength, vrom_begin = FirstLineLength + viewer.header.n_rom16k * ROMpageSize;

                        if(offs_now > vrom_begin)     aim_pos = FontHeight * (1 + (vrom_begin-FirstLineLength) / double(CharsPerLine));
                        else if(offs_now > rom_begin) aim_pos = FontHeight * (1 + (rom_begin-FirstLineLength) / double(CharsPerLine));
                        else aim_pos = 0;
                        scroll = true;
                        break;
                    }
                    case SDLK_END: k_e:
                    {
                        auto pagebeginpos = [=](double p) -> double
                        {
                            double r = (1 + (p-FirstLineLength) / (double)CharsPerLine) * FontHeight - viewport;
                            if(r < 0) r = 0;
                            return r;
                        };

                        long offs_now  = viewer.GetBeginOffset((aim_pos + viewport) / FontHeight + 1);
                        long vrom_begin = FirstLineLength + viewer.header.n_rom16k * ROMpageSize;
                        long rom_end    = pagebeginpos(vrom_begin);
                        long image_end  = pagebeginpos(viewer.image.size());

                        if(offs_now < vrom_begin) aim_pos = rom_end;
                        else aim_pos = image_end;

                        scroll = true;
                        break;
                    }
                    case SDLK_ESCAPE:
                    {
                        SDL_Quit();
                        return 0;
                    }
                }
                break;
            case SDL_MOUSEMOTION:
                fprintf(stderr, "motion: aim_pos=%g\n", aim_pos);
                mousex = event.motion.x;
                mousey = event.motion.y;
                if(event.motion.state & SDL_BUTTON_LMASK)
                {
                    aim_pos -= event.motion.yrel;
                    SDL_ShowCursor(SDL_DISABLE);
                }
                else
                {
                    viewer.MakeStatusDirty();
                    SDL_ShowCursor(SDL_ENABLE);
                }
                break;
            case SDL_MOUSEWHEEL:
                aim_pos = scroll_pos - event.wheel.y * int(FontHeight * 32);
                scroll = true;
                break;
        }

        if(aim_pos < 0) aim_pos = 0;

#if 1
        scroll_pos = aim_pos;
#else
        if(scroll)
        {
            last_slow = std::chrono::system_clock::now();
            last_pos  = scroll_pos;
        }

        if(true)
        {
            auto now = std::chrono::system_clock::now();
            // every 30 ms we change  x := x*(1-p) + y*p,  p+=q
            //                 i.e.   x := x - x*p + y*p,  p+=q
            //                  or..  x := x + (y-x)*p
            //                  or..  x := y + (x-y)*(1-p)
            //
            // So after 60ms, x is   (x - x*p + y*p) * (1-p-q) + y*(p+q)

            auto delta =
                (std::chrono::duration_cast<std::chrono::milliseconds>(now-last_slow).count()
                 + 10)
                / 1000.0;
            double scroll_factor = 1.0;
            if(delta > 0 && delta < 1)
            {
                scroll_factor = (1.0 - std::cos(std::pow(delta,0.7)*3.141592653)) * 0.5;
            }
            //fprintf(stderr, "delta=%g, scroll_factor=%g\n", delta,scroll_factor);
            scroll_pos = last_pos + (aim_pos - last_pos) * scroll_factor;
            //if(delta >= 1) new_scroll_ok = true;
        }
#endif
        if(scroll_pos < 0) scroll_pos = 0;

        unsigned newscroll = scroll_pos;

        /*if(std::fabs(scroll_speed) < 0.2)
        {
            newscroll = FontHeight * (int)(scroll_pos / FontHeight + 0.5);
            scroll_speed = 0;
        }*/

        if(newscroll != viewer.ScrollBegin)
        {
            viewer.ScrollBegin = newscroll;
            viewer.MakeDirty();
        }
    }

    SDL_StopTextInput();
}
