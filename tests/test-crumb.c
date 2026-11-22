#include "crumb.h"

#include <locale.h>

static void
test_fields(void)
{
    const gchar *line = "Édition//  Fólio grande 12 :  Ôrí  :  voilà: une ‹{note: ici}›";
    CwCrumb crumb;
    g_assert_true(cw_crumb_parse(line, TRUE, &crumb));
    g_assert_true(crumb.valid);
    g_assert_cmpuint(crumb.number, ==, 12);
    g_assert_cmpstr(crumb.label, ==, "Fólio grande");
    g_assert_cmpstr(crumb.title, ==, "Ôrí");
    g_assert_cmpint(crumb.header_start, ==, 11);
    g_assert_cmpint(crumb.header_end, ==, 26);
    g_assert_cmpint(crumb.title_start, ==, 30);
    g_assert_cmpint(crumb.title_end, ==, 33);
    g_assert_cmpint(crumb.body_start, ==, 38);
    cw_crumb_clear(&crumb);
}

static void
test_unfinished(void)
{
    CwCrumb crumb;
    g_assert_true(cw_crumb_parse("Crumb 2: : ", FALSE, &crumb));
    g_assert_cmpstr(crumb.title, ==, "");
    g_assert_cmpint(crumb.header_end, ==, 7);
    g_assert_cmpint(crumb.title_start, ==, 9);
    g_assert_cmpint(crumb.title_end, ==, 9);
    g_assert_cmpint(crumb.body_start, ==, 11);
    cw_crumb_clear(&crumb);

    g_assert_true(cw_crumb_parse("Crumb 5: In progress", FALSE, &crumb));
    g_assert_cmpstr(crumb.title, ==, "In progress");
    g_assert_cmpint(crumb.body_start, ==, 20);
    cw_crumb_clear(&crumb);

    g_assert_true(cw_crumb_parse("Crumb 9::", FALSE, &crumb));
    g_assert_cmpint(crumb.title_start, ==, 8);
    g_assert_cmpint(crumb.body_start, ==, 9);
    cw_crumb_clear(&crumb);
}

static void
test_header_numbers(void)
{
    CwCrumb crumb;
    g_assert_true(cw_crumb_parse("B 18446744073709551615: : ", FALSE, &crumb));
    g_assert_cmpuint(crumb.number, ==, G_MAXUINT64);
    cw_crumb_clear(&crumb);

    const gchar *invalid[] = {
        "", "Unnumbered text", "Crumb 1", "Crumbone: : ", "  5: :",
        "Crumb 0: : ", "Crumb-2: : ", "Crumb 18446744073709551616: : "
    };
    for (guint i = 0; i < G_N_ELEMENTS(invalid); ++i) {
        g_assert_false(cw_crumb_parse(invalid[i], FALSE, &crumb));
        g_assert_false(crumb.valid);
        cw_crumb_clear(&crumb);
    }
}

static void
test_first_line(void)
{
    CwCrumb crumb;
    const gchar *line = "Crumb 1: Link: https://site.example/a";
    g_assert_true(cw_crumb_parse(line, TRUE, &crumb));
    g_assert_cmpstr(crumb.label, ==, "Crumb");
    g_assert_cmpstr(crumb.title, ==, "Link");
    g_assert_cmpint(crumb.header_start, ==, 0);
    cw_crumb_clear(&crumb);
    g_assert_cmpuint(cw_crumb_words(line, TRUE), ==, 4);
    g_assert_cmpuint(cw_crumb_words("Work — here//Crumb 1: Ø: a—b / c", TRUE), ==, 6);
    g_assert_true(cw_crumb_parse("Volume 2: Writing//Nota 1: Ø: x", TRUE, &crumb));
    g_assert_cmpstr(crumb.label, ==, "Nota");
    cw_crumb_clear(&crumb);
    line = "Véase https://sitio.example/a//b — Índice//Cranolo 1: Ida: vuelta";
    g_assert_true(cw_crumb_parse(line, TRUE, &crumb));
    g_assert_cmpstr(crumb.label, ==, "Cranolo");
    g_assert_cmpstr(g_utf8_offset_to_pointer(line, crumb.header_start), ==, "Cranolo 1: Ida: vuelta");
    g_assert_cmpuint(cw_crumb_words(line, TRUE), ==, 4);
    cw_crumb_clear(&crumb);
    line = "Notes (https://[::1]/article_(part_(1))//here)//C 1: T: x";
    g_assert_true(cw_crumb_parse(line, TRUE, &crumb));
    g_assert_cmpstr(crumb.label, ==, "C");
    cw_crumb_clear(&crumb);
    g_assert_false(cw_crumb_parse("Link //example.org in prose.", TRUE, &crumb));
    g_assert_cmpint(crumb.record_start, ==, 0);
    cw_crumb_clear(&crumb);
    g_assert_false(cw_crumb_parse("http://site.example/a//b", TRUE, &crumb));
    g_assert_cmpint(crumb.record_start, ==, 0);
    cw_crumb_clear(&crumb);
    g_assert_false(cw_crumb_parse("Work title//", TRUE, &crumb));
    g_assert_cmpint(crumb.record_start, ==, 12);
    cw_crumb_clear(&crumb);
}

/* The installed wc determines locale-dependent treatment of Unicode spaces. */
static void
test_native_wc(void)
{
    gchar sample[] = "Fólio 4: : a—/—b / / c\t/\td\u2002e\u00a0f\u202fg\u2007h\u2060i";
    gchar *args[] = {
        "/bin/sh", "-c",
        "printf '%s\\n' \"$1\" | sed -e 's/—/ /g' -e 's@ / @ @g' | wc -w",
        "crumb-word-test", sample, NULL
    };
    gchar *output = NULL;
    gint status;
    GError *error = NULL;
    g_assert_true(g_spawn_sync(NULL, args, NULL, G_SPAWN_DEFAULT, NULL, NULL,
                               &output, NULL, &status, &error));
    g_assert_no_error(error);
    g_assert_true(g_spawn_check_wait_status(status, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(cw_crumb_words(sample, FALSE), ==, g_ascii_strtoull(output, NULL, 10));
    g_free(output);
}

static void
test_words(void)
{
    const struct { const gchar *line; guint expected; } cases[] = {
        {"", 0}, {"   ", 0}, {"Crumb 1: : ", 3},
        {"Fólio grande 7: Ø: α—β / γ", 7},
        {"Crumb 2: Rhythm: a / / b", 6},
        {"Crumb 2: : a /  / b", 5},
        {"Crumb 3: T: a—/—b", 5},
        {"Crumb 4: : a/b a /b a/ b", 8},
        {"Crumb 5: : a\t/\tb ‹{x y}›", 8},
        {"Crumb 6: ‹{Une note}›: they're well-known.", 6}
    };
    for (guint i = 0; i < G_N_ELEMENTS(cases); ++i)
        g_assert_cmpuint(cw_crumb_words(cases[i].line, FALSE), ==, cases[i].expected);
}

int
main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/crumb/fields", test_fields);
    g_test_add_func("/crumb/unfinished", test_unfinished);
    g_test_add_func("/crumb/header-numbers", test_header_numbers);
    g_test_add_func("/crumb/first-line", test_first_line);
    g_test_add_func("/crumb/word-count", test_words);
    g_test_add_func("/crumb/native-wc", test_native_wc);
    return g_test_run();
}
