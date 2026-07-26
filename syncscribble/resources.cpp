#include "resources.h"
#include <fstream>
#include <unordered_map>
#include "ulib/miniz_gzip.h"
#include "usvg/svgparser.h"
#include "scribbleapp.h"
#include "scribbleconfig.h"


// String resources: `python embed.py icons/*.svg > res_icons.cpp` (for example) to build resource file
struct ResourceItem
{
  const void* data;
  size_t len;
  ResourceItem(const char* d) : data(d), len(0) {}
  ResourceItem(const void* d, size_t l) : data(d), len(l) {}
};

typedef std::unordered_map<std::string, ResourceItem> ResourceMap;
static ResourceMap resourceMap;

static void addStringResources(std::initializer_list<ResourceMap::value_type> values)
{
  resourceMap.insert(values);
}

static const char* getResource(const std::string& name)
{
  auto it = resourceMap.find(name);
  return it != resourceMap.end() ? (const char*)it->second.data : NULL;
}

static ConstMemStream getResourceStream(const std::string& name)
{
  auto it = resourceMap.find(name);
  return it != resourceMap.end() ? ConstMemStream(it->second.data, it->second.len) : ConstMemStream("", 0);
}

// SVG for icons - the attempted trick of using static object constructor to load resources automatically
//  doesn't work in general because if a translation unit doesn't have any fns that are called, static
//  objects may never be initialized (and could be removed as dead code)
// See https://github.com/google/material-design-icons for more icons
#define LOAD_RES_FN loadIconRes
#include "scribbleres/res_icons.cpp"

// i18n - note that MSVC maximum string length is only ~16KB (but we use array now, so no problem)
#define LOAD_RES_FN loadStringRes
#include "scribbleres/res_strings.cpp"
//#define loadStringRes() do {} while(0)

// SVG and CSS for GUI
#include "ugui/theme.cpp"

#if PLATFORM_EMSCRIPTEN
#include "Roboto-Regular.c"
#endif

void setupResources()
{
  static std::string guiSVG;
  FSPath sansBackupPath(Application::appDir, "Roboto-Regular.ttf");
  FSPath sansBoldPath;
  FSPath sansItalicPath;
#if PLATFORM_WIN
  const char* sans = "C:/Windows/Fonts/segoeui.ttf";
  FSPath fallbackPath(Application::appDir, "DroidSansFallback.ttf");
  const char* fallback = fallbackPath.c_str();
  //sansBoldPath = FSPath("C:/Windows/Fonts/segoeuib.ttf");
  //sansItalicPath = FSPath("C:/Windows/Fonts/segoeuii.ttf");
#elif PLATFORM_OSX
  const char* fontpath = Application::appDir.c_str();
  FSPath sansPath(fontpath, "SanFranciscoDisplay-Regular.otf");
  FSPath fallbackPath(fontpath, "DroidSansFallback.ttf");
  const char* sans = sansPath.c_str();
  const char* fallback = fallbackPath.c_str();
#elif PLATFORM_ANDROID
  const char* extStorage = SDL_AndroidGetExternalStoragePath();
  if(!extStorage)
    extStorage = "/sdcard/Android/data/ca.helu.eidolon/files";  // prevent crash
  const char* sans = "/system/fonts/Roboto-Regular.ttf";
  sansBackupPath = FSPath(extStorage, ".saved/Roboto-Regular.ttf");
  //const char* sansbold = "/system/fonts/Roboto-Bold.ttf";
  FSPath fallbackPath(extStorage, ".saved/DroidSansFallback.ttf");
  const char* fallback = fallbackPath.c_str();
#elif PLATFORM_IOS
  // see stackoverflow.com/questions/3692812/ and gist.github.com/1892760 for getting iOS system fonts
  // SF font downloaded from github.com/AppleDesignResources/SanFranciscoFont/
  const char* sans = "SanFranciscoDisplay-Regular.otf";
  const char* fallback = "DroidSansFallback.ttf";
#elif PLATFORM_LINUX
  FSPath fontpath(Application::appDir);
  FSPath sansPath = fontpath.child("Roboto-Regular.ttf");
  if(!sansPath.exists()) {
     fontpath = FSPath(Application::appDir, "../../scribbleres/fonts/");
     sansPath = fontpath.child("Roboto-Regular.ttf");
  }
  FSPath fallbackPath = fontpath.child("DroidSansFallback.ttf");
  const char* sans = sansPath.c_str();
  const char* fallback = fallbackPath.c_str();
  sansBackupPath = FSPath("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
  //sansItalicPath = FSPath(fontpath, "Roboto-Italic.ttf");
#elif PLATFORM_EMSCRIPTEN
  Painter::loadFontMem("ui-sans", Roboto_Regular_ttf, Roboto_Regular_ttf_len);
  const char* sans = NULL;
  const char* fallback = NULL;
#endif
  if(sans && !Painter::loadFont("ui-sans", sans) && !Painter::loadFont("ui-sans", sansBackupPath.c_str())) {
    if(Application::runApplication)
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, _("Error"),
          _("Unable to load fonts.  Please reinstall Write."),  NULL);
    PLATFORM_LOG("Could not add font sans from %s\n", sans);
  }
  // Also, looks like OTF (cubic beziers) is smaller than TTF (quads), so try to use that for embedded fonts
  // DroidSansFallbackFull adds some rare characters but removes Korean, so use DroidSansFallback
  // ... but neither includes ASCII chars, so we still need a primary font!
  if(fallback && Painter::loadFont("fallback", fallback))
    Painter::addFallbackFont(NULL, "fallback");  // base font = NULL to set as global fallback
  // load bold and italic fonts if requested
  if(!sansBoldPath.isEmpty())
    Painter::loadFont("ui-sans-bold", sansBoldPath.c_str());
  if(!sansItalicPath.isEmpty())
    Painter::loadFont("ui-sans-italic", sansItalicPath.c_str());
  // load user fallbacks
  const char* userFontsStr = ScribbleApp::cfg->String("userFonts", "");
  auto userFonts = splitStringRef(StringRef(userFontsStr), ";", true);
  for(const StringRef& userFont : userFonts) {
    auto name_path = splitStringRef(userFont, ":");
    // some way to request user as fallback?
    //Painter::addFallbackFont("sans", fallbackname.c_str());
    if(name_path.size() < 2 || !Painter::loadFont(name_path[0].toString().c_str(), name_path[1].toString().c_str()))
      PLATFORM_LOG("Failed to load font %s\n", userFont.toString().c_str());
  }

  // Fonts for the text box tool (TextBoxDialog::fontFamilies()).  Family names map to fonts loaded under the
  // same name; a "<name>-bold"/"-italic"/"-bold-italic" face is used for bold/italic if loaded (setFontFamily
  // in painter.cpp), else bold is synthesized from font-weight and italic falls back to the regular face.
  // Each family is loaded from the first available candidate path; missing families fall back to ui-sans.
  auto loadFontFamily = [](const char* name, std::initializer_list<const char*> paths) {
    for(const char* p : paths) {
      if(p && p[0] && Painter::loadFont(name, p))
        return;
    }
  };
  // Note: an unrecognized family (e.g. "sans-serif" when no face below is loaded) falls back to the
  // always-available bundled ui-sans font, so it is safe to leave families unloaded.
#if PLATFORM_LINUX
  loadFontFamily("sans-serif", {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"});
  loadFontFamily("sans-serif-bold", {"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"});
  loadFontFamily("sans-serif-italic", {"/usr/share/fonts/truetype/dejavu/DejaVuSans-Oblique.ttf"});
  loadFontFamily("sans-serif-bold-italic", {"/usr/share/fonts/truetype/dejavu/DejaVuSans-BoldOblique.ttf"});
  loadFontFamily("serif", {"/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf"});
  loadFontFamily("serif-bold", {"/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSerif-Bold.ttf"});
  loadFontFamily("serif-italic", {"/usr/share/fonts/truetype/dejavu/DejaVuSerif-Italic.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSerif-Italic.ttf"});
  loadFontFamily("monospace", {"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf"});
  loadFontFamily("monospace-bold", {"/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"});
  loadFontFamily("monospace-italic", {"/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Oblique.ttf"});
#elif PLATFORM_WIN
  loadFontFamily("sans-serif", {"C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf"});
  loadFontFamily("sans-serif-bold", {"C:/Windows/Fonts/segoeuib.ttf", "C:/Windows/Fonts/arialbd.ttf"});
  loadFontFamily("sans-serif-italic", {"C:/Windows/Fonts/segoeuii.ttf", "C:/Windows/Fonts/ariali.ttf"});
  loadFontFamily("sans-serif-bold-italic", {"C:/Windows/Fonts/segoeuiz.ttf", "C:/Windows/Fonts/arialbi.ttf"});
  loadFontFamily("serif", {"C:/Windows/Fonts/times.ttf"});
  loadFontFamily("serif-bold", {"C:/Windows/Fonts/timesbd.ttf"});
  loadFontFamily("serif-italic", {"C:/Windows/Fonts/timesi.ttf"});
  loadFontFamily("serif-bold-italic", {"C:/Windows/Fonts/timesbi.ttf"});
  loadFontFamily("monospace", {"C:/Windows/Fonts/consola.ttf", "C:/Windows/Fonts/cour.ttf"});
  loadFontFamily("monospace-bold", {"C:/Windows/Fonts/consolab.ttf", "C:/Windows/Fonts/courbd.ttf"});
  loadFontFamily("monospace-italic", {"C:/Windows/Fonts/consolai.ttf", "C:/Windows/Fonts/couri.ttf"});
#elif PLATFORM_OSX
  loadFontFamily("serif", {"/System/Library/Fonts/Supplemental/Times New Roman.ttf",
      "/Library/Fonts/Times New Roman.ttf"});
  loadFontFamily("serif-bold", {"/System/Library/Fonts/Supplemental/Times New Roman Bold.ttf"});
  loadFontFamily("serif-italic", {"/System/Library/Fonts/Supplemental/Times New Roman Italic.ttf"});
  loadFontFamily("monospace", {"/System/Library/Fonts/Menlo.ttc", "/System/Library/Fonts/Courier.ttc"});
#elif PLATFORM_ANDROID
  // Modern Android ships Roboto as a single variable font (no separate bold/italic files), so sans-serif
  //  bold is synthesized and italic falls back to regular.  NotoSerif still has all four static faces.
  loadFontFamily("sans-serif", {"/system/fonts/Roboto-Regular.ttf", "/system/fonts/RobotoStatic-Regular.ttf"});
  loadFontFamily("serif", {"/system/fonts/NotoSerif-Regular.ttf"});
  loadFontFamily("serif-bold", {"/system/fonts/NotoSerif-Bold.ttf"});
  loadFontFamily("serif-italic", {"/system/fonts/NotoSerif-Italic.ttf"});
  loadFontFamily("serif-bold-italic", {"/system/fonts/NotoSerif-BoldItalic.ttf"});
  loadFontFamily("monospace", {"/system/fonts/DroidSansMono.ttf", "/system/fonts/CutiveMono.ttf"});
#endif

  loadIconRes();
  // hook to support loading from resources; can we move this somewhere to deduplicate w/ other projects?
  SvgParser::openStream = [](const char* name) -> std::istream* {
    if(name[0] == ':' && name[1] == '/')
      name += 2;
    const char* res = getResource(name);
    if(res)
      return new std::istringstream(res);
    // if getResource() fails, we should do this to support loading resources from application dir:
    //FSPath(Application::appDir, name+2)

    if(FSPath(name).extension() == "svgz") {
      std::fstream gzin(PLATFORM_STR(name), std::ios_base::in | std::ios_base::binary);
      std::stringstream* gzout = new std::stringstream;
      if(gunzip(gzin, *gzout) > 0 || gzout->tellp() > 0) {
        gzout->seekg(0);
        return gzout;  // caller takes ownership
      }
      delete gzout;
    }
    return new std::ifstream(PLATFORM_STR(name), std::ios_base::in | std::ios_base::binary);
  };

  // GUI style
  guiSVG = ScribbleApp::cfg->String("guiSVG");
  if(!guiSVG.empty() && guiSVG.find('\n') == std::string::npos)
    guiSVG = readFile(guiSVG.c_str());

  SvgDocument* widgetDoc = SvgParser().parseString(guiSVG.empty() ? defaultWidgetSVG : guiSVG.c_str());
  setGuiResources(widgetDoc);
}

SvgCssStylesheet* createStylesheet()
{
  std::string guiCSS = ScribbleApp::cfg->String("guiCSS");
  if(!guiCSS.empty() && guiCSS.find('\n') == std::string::npos)
    guiCSS = readFile(guiCSS.c_str());

  SvgCssStylesheet* styleSheet = new SvgCssStylesheet;
  if(guiCSS.empty()) {
    styleSheet->parse_stylesheet(defaultColorsCSS);
    styleSheet->parse_stylesheet(defaultStyleCSS);
  }
  else
    styleSheet->parse_stylesheet(guiCSS.c_str());
  styleSheet->sort_rules();
  return styleSheet;
}

static std::unordered_map<std::string, std::string> translationMap;
static std::unordered_map<std::string, std::string> revTranslationMap;

bool setupI18n(const char* lc)
{
  pugi::xml_document trDoc;
  pugi::xml_document revTrDoc;
  std::string locale(lc);
  loadStringRes();

  const char* lcfile = ScribbleApp::cfg->String("translations", "");
  if(lcfile[0])
    trDoc.load_file(PLATFORM_STR(lcfile));
  else if(!locale.empty()) {
    locale[2] = '-';  // some platforms might use '_' as separator
    ConstMemStream lcstrings = getResourceStream("strings/strings-" + locale + ".xml");  // language + region
    if(!lcstrings.size())
      lcstrings = getResourceStream("strings/strings-" + locale.substr(0,2) + ".xml");  // language only
    if(lcstrings.size()) {
      MemStream gzout(lcstrings.size() * 8);
      gunzip(minigz_io_t(lcstrings), minigz_io_t(gzout));
      trDoc.load_buffer(gzout.data(), gzout.size());
    }
  }

  int showKeys = ScribbleApp::cfg->Int("showTranslationKeys");
  const char* enfile = ScribbleApp::cfg->String("revTranslations", "");
  if(enfile[0])
    revTrDoc.load_file(PLATFORM_STR(enfile));
  else if(trDoc.child("resources") || showKeys) {
    ConstMemStream enstrings = getResourceStream("strings/strings.xml");
    if(enstrings.size()) {
      MemStream gzout(enstrings.size() * 8);
      gunzip(minigz_io_t(enstrings), minigz_io_t(gzout));
      revTrDoc.load_buffer(gzout.data(), gzout.size());
      if(showKeys > 1)
        std::cout << std::string(gzout.data(), gzout.size());  // string has '%', avoid printf!
    }
  }

  // note than emplace doesn't replace an existing entry, so values at top of file override duplicates below!
  pugi::xml_document& trDocRef = showKeys ? revTrDoc : trDoc;
  for(auto res = trDocRef.child("resources").first_child(); res; res = res.next_sibling()) {
    if(strcmp(res.name(), "string") != 0) continue;
    const char* key = res.attribute("name").value();
#if IS_DEBUG
    if(translationMap.find(key) != translationMap.end())
      PLATFORM_LOG("Duplicate translation key: %s\n", key);
#endif
    translationMap.emplace(key, showKeys ? key : res.first_child().value());
  }
  for(auto res = revTrDoc.child("resources").first_child(); res; res = res.next_sibling()) {
    if(strcmp(res.name(), "string") == 0)
      revTranslationMap.emplace(res.first_child().value(), res.attribute("name").value());
  }
  return !translationMap.empty();
}

// _() is standard (from GNU gettext) ... but perhaps we should consider _T(), _t(), tr(), TR(), ...?
// `s` can be translation key or English text (if revTranslationMap is also filled)
const char* _(const char* s)
{
  //auto ss = new std::string("@");  ss->append(s);  return ss->c_str();  // for finding missed strings visually
  if(translationMap.empty())
    return s;
  auto it = translationMap.find(s);
  if(it != translationMap.end())  // s is a translation key
    return it->second.c_str();
  auto revit = revTranslationMap.find(s);
  if(revit != revTranslationMap.end())
    it = translationMap.find(revit->second);
  if(it != translationMap.end())  // s is English text, *revit is translation key
    return it->second.c_str();
#if IS_DEBUG
  static std::unordered_map<std::string, std::string> missing;
  if(missing.find(s) == missing.end()) {
    PLATFORM_LOG("Missing translation: %s\n", s);
    missing[s] = "?";
  }
#endif
  return s;
}
