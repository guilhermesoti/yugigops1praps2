/* No system fonts on the PS2: every lookup finds nothing, and the callers
 * fall back to their own (or draw nothing). */
#ifndef MEMORIES_PS2_FONTCONFIG_H
#define MEMORIES_PS2_FONTCONFIG_H
typedef unsigned char FcChar8;
typedef int FcBool;
typedef struct _FcPattern FcPattern;
typedef struct _FcConfig FcConfig;
typedef struct _FcFontSet { int nfont, sfont; FcPattern **fonts; } FcFontSet;
typedef struct _FcObjectSet FcObjectSet;
typedef enum { FcResultMatch, FcResultNoMatch, FcResultTypeMismatch, FcResultNoId, FcResultOutOfMemory } FcResult;
typedef enum { FcMatchPattern, FcMatchFont, FcMatchScan } FcMatchKind;
typedef struct _FcCharSet FcCharSet;
typedef struct _FcLangSet FcLangSet;
#define FC_FILE "file"
#define FC_FAMILY "family"
#define FC_STYLE "style"
#define FC_INDEX "index"
#define FC_LANG "lang"
#define FC_SPACING "spacing"
#define FC_WEIGHT "weight"
#define FC_SLANT "slant"
#define FC_CHARSET "charset"
#define FC_SCALABLE "scalable"
#define FC_OUTLINE "outline"
#define FC_FONTFORMAT "fontformat"
#define FC_MONO 100
#define FC_WEIGHT_BOLD 200
#define FC_WEIGHT_REGULAR 80
#define FC_SLANT_ROMAN 0
#define FcTrue 1
#define FcFalse 0
static inline FcBool FcInit(void) { return 0; }
static inline FcConfig *FcInitLoadConfigAndFonts(void) { return 0; }
static inline void FcFini(void) {}
static inline FcPattern *FcPatternCreate(void) { return 0; }
static inline FcPattern *FcNameParse(const FcChar8 *name) { (void)name; return 0; }
static inline void FcPatternDestroy(FcPattern *p) { (void)p; }
static inline FcBool FcPatternAddString(FcPattern *p, const char *o, const FcChar8 *s) { (void)p; (void)o; (void)s; return 0; }
static inline FcBool FcPatternAddInteger(FcPattern *p, const char *o, int i) { (void)p; (void)o; (void)i; return 0; }
static inline FcBool FcPatternAddBool(FcPattern *p, const char *o, FcBool b) { (void)p; (void)o; (void)b; return 0; }
static inline FcBool FcPatternAddCharSet(FcPattern *p, const char *o, const FcCharSet *c) { (void)p; (void)o; (void)c; return 0; }
static inline FcBool FcPatternAddLangSet(FcPattern *p, const char *o, const FcLangSet *l) { (void)p; (void)o; (void)l; return 0; }
static inline FcBool FcConfigSubstitute(FcConfig *c, FcPattern *p, FcMatchKind k) { (void)c; (void)p; (void)k; return 0; }
static inline void FcDefaultSubstitute(FcPattern *p) { (void)p; }
static inline FcPattern *FcFontMatch(FcConfig *c, FcPattern *p, FcResult *r) { (void)c; (void)p; if (r) *r = FcResultNoMatch; return 0; }
static inline FcFontSet *FcFontSort(FcConfig *c, FcPattern *p, FcBool t, FcCharSet **cs, FcResult *r) { (void)c; (void)p; (void)t; (void)cs; if (r) *r = FcResultNoMatch; return 0; }
static inline FcFontSet *FcFontList(FcConfig *c, FcPattern *p, FcObjectSet *o) { (void)c; (void)p; (void)o; return 0; }
static inline void FcFontSetDestroy(FcFontSet *s) { (void)s; }
static inline FcObjectSet *FcObjectSetBuild(const char *first, ...) { (void)first; return 0; }
static inline void FcObjectSetDestroy(FcObjectSet *o) { (void)o; }
static inline FcResult FcPatternGetString(const FcPattern *p, const char *o, int n, FcChar8 **s) { (void)p; (void)o; (void)n; (void)s; return FcResultNoMatch; }
static inline FcResult FcPatternGetInteger(const FcPattern *p, const char *o, int n, int *i) { (void)p; (void)o; (void)n; (void)i; return FcResultNoMatch; }
static inline FcResult FcPatternGetBool(const FcPattern *p, const char *o, int n, FcBool *b) { (void)p; (void)o; (void)n; (void)b; return FcResultNoMatch; }
static inline FcResult FcPatternGetCharSet(const FcPattern *p, const char *o, int n, FcCharSet **c) { (void)p; (void)o; (void)n; (void)c; return FcResultNoMatch; }
static inline FcCharSet *FcCharSetCreate(void) { return 0; }
static inline FcBool FcCharSetAddChar(FcCharSet *c, unsigned u) { (void)c; (void)u; return 0; }
static inline FcBool FcCharSetHasChar(const FcCharSet *c, unsigned u) { (void)c; (void)u; return 0; }
static inline void FcCharSetDestroy(FcCharSet *c) { (void)c; }
static inline FcLangSet *FcLangSetCreate(void) { return 0; }
static inline FcBool FcLangSetAdd(FcLangSet *l, const FcChar8 *s) { (void)l; (void)s; return 0; }
static inline void FcLangSetDestroy(FcLangSet *l) { (void)l; }
static inline FcConfig *FcConfigGetCurrent(void) { return 0; }
static inline FcBool FcConfigAppFontAddFile(FcConfig *c, const FcChar8 *f) { (void)c; (void)f; return 0; }
static inline FcBool FcConfigAppFontAddDir(FcConfig *c, const FcChar8 *f) { (void)c; (void)f; return 0; }
#endif
