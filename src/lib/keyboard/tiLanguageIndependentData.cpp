#include <lib/textinput/tiLanguageIndependentData.h>
#include <lib/textinput/tiCpData.h>

namespace textinput {
namespace keyboard {
namespace cellphonetype {

const wchar16_t *csSpaceZero = WSTR("\xE057")
                             WSTR("0");
const wchar16_t *csSpace[2] = {WSTR("\xE057"), NULL};

} // namespace cellphonetype
} // namespace keyboard

namespace langindependent {

const wchar16_t *cLanguageIndependentString[LANG_STRID_MAX][LanguageEnd] = {
    {WSTR("OK"), WSTR("OK"), WSTR("OK"), WSTR("OK"), WSTR("O.K."), WSTR("OK"), WSTR("Aceptar"), WSTR("Ok\x00E9")},
    {WSTR("\x3084\x3081\x308B"), WSTR("Quit"), WSTR("Quit"), WSTR("Quitter"), WSTR("Abbruch"),
     WSTR("Annulla"), WSTR("Salir"), WSTR("Annuleren")},
    {WSTR("\x4E88\x6E2C\x8F9E\x66F8\x8A00\x8A9E\x306E\x9078\x629E"), WSTR("Dictionary"),
     WSTR("Language"), WSTR("Langue"), WSTR("Sprachauswahl"), WSTR("Lingua"), WSTR("Elegir idioma"),
     WSTR("Taalkeuze")},
    {WSTR("\x3064\x304E"), WSTR("\x2192"), WSTR("\x2192"), WSTR("\x2192"), WSTR("\x2192"), WSTR("\x2192"),
     WSTR("\x2192"), WSTR("\x2192")},
    {WSTR("\x307E\x3048"), WSTR("\x2190"), WSTR("\x2190"), WSTR("\x2190"), WSTR("\x2190"), WSTR("\x2190"),
     WSTR("\x2190"), WSTR("\x2190")},
    {WSTR("a"), WSTR("a"), WSTR("a"), WSTR("a"), WSTR("a"), WSTR("a"), WSTR("a"), WSTR("a")},
    {WSTR("\x3068\x3058\x308B"), WSTR("Close"), WSTR("Close"), WSTR("Fermer"),
     WSTR("Schlie\x00DF")
     WSTR("en"),
     WSTR("Chiudi"), WSTR("Cerrar"), WSTR("Sluiten")},
    {WSTR("\x8A18\x53F7"), WSTR("More"), WSTR("\x20AC\x00E9\x03B1"), WSTR("\x20AC\x00E5\x03B1"),
     WSTR("\x20AC\x00E9\x03B1"), WSTR("\x20AC\x00E5\x03B1"), WSTR("\x20AC\x00E5\x03B1"),
     WSTR("\x20AC\x00E9\x03B1")},
    {WSTR("Space"), WSTR("Space"), WSTR("Space"), WSTR(""), WSTR(""), WSTR(""), WSTR(""), WSTR("Space")},
    {WSTR("\x3057\x307E\x3046"), WSTR("Back"), WSTR("Back"), WSTR("Fermer"),
     WSTR("Zur\x00FC")
     WSTR("ck"),
     WSTR("Chiudi"), WSTR("Cerrar"), WSTR("Stoppen")},
};

} // namespace langindependent
} // namespace textinput
