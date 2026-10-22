/* Diagnostic: can LSan traverse a reachable Fontconfig pattern's elements? */
#include <assert.h>
#include <fontconfig/fontconfig.h>
#include <sanitizer/lsan_interface.h>
#include <stdio.h>

static FcPattern *retained;

__attribute__((noinline)) static void create_pattern(void) {
    retained = FcPatternCreate();
    assert(retained);
    assert(FcPatternAddString(retained, FC_FAMILY, (const FcChar8 *)"Tarn diagnostic"));
}

__attribute__((noinline)) static void verify_and_destroy(void) {
    FcChar8 *family = NULL;
    assert(FcPatternGetString(retained, FC_FAMILY, 0, &family) == FcResultMatch);
    printf("reachable pattern family: %s\n", family);
    FcPatternDestroy(retained);
    retained = NULL;
}

int main(void) {
    create_pattern();
    int retained_report = __lsan_do_recoverable_leak_check();
    printf("LSan while pattern is reachable: %d\n", retained_report);
    verify_and_destroy();
    int destroyed_report = __lsan_do_recoverable_leak_check();
    printf("LSan after pattern destruction: %d\n", destroyed_report);
    /* The first result is evidence, not a required Fontconfig implementation detail. */
    return destroyed_report != 0;
}
